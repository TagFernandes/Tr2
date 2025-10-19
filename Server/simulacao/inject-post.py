import http.client  # <- Biblioteca padrão para ser um cliente HTTP
import json
import socket       # <- Biblioteca padrão para sockets (usada para timeouts)
import time
import random
from datetime import datetime

# --- Configurações ---
# O endpoint do seu servidor
HOST = "localhost"
PORT = 8000
PATH = "/sensor"

# Intervalo em segundos (5 minutos = 300 segundos)
INTERVALO_SEGUNDOS = 5 * 60

def gerar_dados_sensor():
    """Gera um dicionário com dados fictícios de sensor."""
    temp = round(18.0 + random.random() * 10.0, 2) # Temp entre 18.0 e 28.0
    hum = round(50.0 + random.random() * 25.0, 2)  # Humidade entre 50.0 e 75.0
    dust = round(10.0 + random.random() * 15.0, 2) # Poeira entre 10.0 e 25.0
    
    data = {
        "sensor": "sensor_simulado",
        "temperature": temp,
        "humidity": hum,
        "dust": dust
    }
    return data

def enviar_dados(data):
    """Envia os dados para o endpoint via POST JSON usando http.client."""
    now_str = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    
    conn = None  # Inicializa a conexão fora do try para o finally
    try:
        # 1. Serializar os dados para JSON e depois para bytes
        json_data = json.dumps(data)
        json_data_bytes = json_data.encode('utf-8')

        # 2. Definir os cabeçalhos (headers)
        headers = {
            'Content-Type': 'application/json',
            'Content-Length': str(len(json_data_bytes)),
            'Host': f"{HOST}:{PORT}" # Adicionado para robustez
        }

        # 3. Criar a conexão com o servidor
        # O timeout de 10s é tratado pela biblioteca socket
        conn = http.client.HTTPConnection(HOST, PORT, timeout=10)

        # 4. Enviar a requisição POST
        conn.request("POST", PATH, body=json_data_bytes, headers=headers)

        # 5. Obter a resposta do servidor
        response = conn.getresponse()
        
        # 6. Ler o corpo da resposta
        response_body = response.read().decode('utf-8')

        # 7. Verificar se a requisição foi bem-sucedida
        if response.status == 200:
            print(f"[{now_str}] Dados enviados com sucesso: {data}")
            try:
                # Tenta formatar a resposta como JSON
                print(f"    Resposta do servidor: {json.loads(response_body)}")
            except json.JSONDecodeError:
                # Se não for JSON, apenas imprime o texto
                print(f"    Resposta do servidor: {response_body}")
        else:
            print(f"[{now_str}] Erro ao enviar dados. Status: {response.status} {response.reason}")
            print(f"    Resposta: {response_body}")
    
    # 8. Tratar erros de conexão e timeout
    except ConnectionRefusedError:
        print(f"[{now_str}] ERRO: Nao foi possivel conectar ao servidor em http://{HOST}:{PORT}{PATH}.")
        print("    (ConnectionRefusedError) Verifique se o 'server_timescale.py' esta rodando.")
    except socket.timeout:
        print(f"[{now_str}] ERRO: Timeout (10s) ao tentar conectar.")
    except http.client.HTTPException as e:
        print(f"[{now_str}] ERRO HTTP desconhecido: {e}")
    except Exception as e:
        print(f"[{now_str}] ERRO inesperado: {e}")
    
    finally:
        # 9. Garantir que a conexão seja sempre fechada
        if conn:
            conn.close()

# --- Loop Principal ---
if __name__ == "__main__":
    print(f"Iniciando simulador de sensor (usando http.client).")
    print(f"Enviando dados para http://{HOST}:{PORT}{PATH} a cada {INTERVALO_SEGUNDOS / 60:.0f} minutos.")
    print("Pressione CTRL+C para parar.")
    
    while True:
        try:
            # 1. Gerar os dados
            dados = gerar_dados_sensor()
            
            # 2. Enviar os dados
            enviar_dados(dados)
            
            # 3. Esperar pelo próximo ciclo
            print(f"\nAguardando {INTERVALO_SEGUNDOS / 60:.0f} minutos para o proximo envio...")
            time.sleep(INTERVALO_SEGUNDOS)
            
        except KeyboardInterrupt:
            print("\nPrograma interrompido pelo usuario. Encerrando...")
            break