import http.server
import socketserver
import json
import psycopg2
import logging
from loki_logger_handler.loki_logger_handler import LokiLoggerHandler
import time
from datetime import datetime, timezone

#Log
handler = LokiLoggerHandler(
    url="http://localhost:3101/loki/api/v1/push",
    labels={"application": "ServerTr2", "environment": "production"},
)
logger = logging.getLogger("ServidorTr2")
logger.setLevel(logging.INFO)
logger.addHandler(handler)

# --- Configurações de Conexão com banco ---
DB_NAME = "tr2_banco"
DB_USER = "admin"
DB_PASS = "1234"
DB_HOST = "localhost" # script está fora do Docker
DB_PORT = "5433"     

# --- Configurações do Servidor ---
SERVER_HOST = '0.0.0.0'
SERVER_PORT = 8000

# -- Salvar ultima leitura de cada sensor
last_readings = {}
TEMPO_MAX_ULTIMA_LEITURA = 1800  # 30 min
# --- Funções do Banco de Dados ---

def connect_db():
    """Tenta se conectar ao banco de dados até ter sucesso."""
    conn = None
    while conn is None:
        try:
            conn = psycopg2.connect(
                dbname=DB_NAME,
                user=DB_USER,
                password=DB_PASS,
                host=DB_HOST,
                port=DB_PORT
            )
            logger.info("Conexão com o TimescaleDB estabelecida com sucesso!")
        except psycopg2.OperationalError as e:
            logger.warning(f"Erro ao conectar: {e}. O banco está pronto?")
            logger.warning("Tentando novamente em 5 segundos...")
            time.sleep(5)
    return conn

def setup_database(conn):
    """
    Cria a tabela de dados (sensor_data) e a converte em 
    uma Hypertable do TimescaleDB.
    """
    with conn.cursor() as cursor:
        # Tabela para os dados de temperatura, umidade e poeira
        create_table_sql = """
        CREATE TABLE IF NOT EXISTS sensor_data (
            time        TIMESTAMPTZ       NOT NULL,
            sensor_id   TEXT              NOT NULL,
            temperature DOUBLE PRECISION  NULL,
            humidity    DOUBLE PRECISION  NULL,
            dust        DOUBLE PRECISION  NULL
        );
        """
        cursor.execute(create_table_sql)
        
        create_hypertable_sql = """
        SELECT create_hypertable(
            'sensor_data', 'time', 
            if_not_exists => TRUE
        );
        """
        try:
            cursor.execute(create_hypertable_sql)
        except psycopg2.Error as e:
            if "already exists" in str(e) or "table is not empty" in str(e):
                conn.rollback() # Cancela a transação de erro
            else:
                raise e 

        conn.commit()
        logger.info("Banco de dados configurado com sucesso.")

# --- Servidor HTTP ---

try:
    db_connection = connect_db()
    setup_database(db_connection)
except Exception as e:
    logger.critical(f"Falha ao configurar o banco de dados na inicialização: {e}")
    exit(1)


class SensorRequestHandler(http.server.BaseHTTPRequestHandler):
    
    def do_GET(self):
        if self.path == '/':
            self.send_response(200)
            self.send_header('Content-type', 'text/plain')
            self.end_headers()
            self.wfile.write(b"Servidor de sensores no ar. Use POST em /sensor.")
        elif self.path == '/status':
            # Rota solicitada: retorna json {"value": 1}
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.end_headers()

            sensor_status = {}
            now_utc = datetime.now(timezone.utc)
            
            for sensor, last_time_str in last_readings.items():
                try:
                    # Converte string ISO de volta para datetime (com timezone)
                    last_time = datetime.fromisoformat(last_time_str)
                    
                    # Calcula a diferença em segundos
                    delta = (now_utc - last_time).total_seconds()
                    
                    if delta > TEMPO_MAX_ULTIMA_LEITURA:
                        # Passou do tempo limite (sensor offline/mudo)
                        sensor_status[sensor] = 0
                    else:
                        # Dentro do tempo limite (sensor online)
                        sensor_status[sensor] = 1
                        
                except Exception as e:
                    logger.error(f"Erro ao calcular status do sensor {sensor}: {e}")
                    sensor_status[sensor] = 0

            self.wfile.write(json.dumps(sensor_status).encode('utf-8'))
        else:
            self.send_error(404, "Nao encontrado")

    def do_POST(self):
        """Responde a requisições POST com dados JSON"""
        if self.path == '/sensor':
            try:
                # 1. Ler o JSON
                content_length = int(self.headers['Content-Length'])
                post_data_bytes = self.rfile.read(content_length)
                data = json.loads(post_data_bytes.decode('utf-8'))
                
                # 2. Obter dados do JSON
                sensor_id = data.get('sensor')
                temp = data.get('temperature')
                hum = data.get('humidity')
                dust = data.get('dust', -1)
                
                
                if temp is None or hum is None or dust is None or sensor_id is None:
                    error_msg = "JSON invalido. Faltando chaves: 'sensor', 'temperature', 'humidity', 'dust'"
                    logger.warning(f"JSON mal formatado recebido: {data}. Erro: {error_msg}")
                    self.send_error(400, error_msg)
                    return
                
                # 3. Preparar e inserir no banco
                now = datetime.now(timezone.utc)
                
                # Salva o horário atual para este sensor no dicionário global
                last_readings[sensor_id] = now.isoformat()

                sql = """
                INSERT INTO sensor_data (time, sensor_id, temperature, humidity, dust)
                VALUES (%s, %s, %s, %s, %s);
                """
                
                with db_connection.cursor() as cursor:
                    cursor.execute(sql, (now, sensor_id, temp, hum, dust))
                    db_connection.commit() 

                logger.info(f"Dados inseridos do sensor '{sensor_id}': Temp={temp}, Hum={hum}, Dust={dust}")
                
                # 4. Responder ao cliente
                self.send_response(200, "OK")
                self.send_header('Content-type', 'application/json')
                self.end_headers()
                self.wfile.write(json.dumps({'status': 'ok'}).encode('utf-8'))

            except Exception as e:
                logger.error(f"Erro inesperado: {e}")
                self.send_error(500, "Erro interno do servidor")
        else:
            self.send_error(404, "Nao encontrado. Use o endpoint /sensor")


if __name__ == "__main__":
    server = http.server.HTTPServer((SERVER_HOST, SERVER_PORT), SensorRequestHandler)
    logger.info(f"Servidor HTTP (single-thread) escutando em http://{SERVER_HOST}:{SERVER_PORT}")
    
    try:
        logger.info("Servidor iniciado")
        print("Servidor inciado")
        server.serve_forever()
    except KeyboardInterrupt:
        logger.warning("\nInterrupcao recebida (Ctrl+C). Encerrando...")
    finally:
        if db_connection:
            db_connection.close()
            logger.info("Conexão com o banco de dados fechada.")
        server.server_close()
        logger.info("Servidor HTTP parado.")
