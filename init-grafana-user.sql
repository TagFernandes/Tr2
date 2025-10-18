/* Este script é executado automaticamente na inicialização do banco.
   Ele cria um usuário 'grafana_reader' com permissões de apenas leitura.
*/

-- 1. Cria o novo usuário (role) com uma senha forte
-- !! TROQUE A SENHA AQUI !!
CREATE USER grafana_reader WITH PASSWORD 'grafana' LOGIN;

-- 2. Permite que o novo usuário se conecte ao banco de dados 'tr2_banco'
-- (O script já está rodando dentro de 'tr2_banco', mas isso é uma boa prática)
GRANT CONNECT ON DATABASE tr2_banco TO grafana_reader;

-- 3. Permite que o usuário acesse o schema 'public' (onde as tabelas ficam)
GRANT USAGE ON SCHEMA public TO grafana_reader;

-- 4. [A PARTE MAIS IMPORTANTE]
-- Concede permissão de SELECT em TODAS AS TABELAS que forem criadas
-- NO FUTURO pelo usuário 'admin' (o usuário que seu script Python usa).
ALTER DEFAULT PRIVILEGES FOR ROLE admin IN SCHEMA public
   GRANT SELECT ON TABLES TO grafana_reader;

-- 5. Concede permissão de SELECT nas tabelas que JÁ EXISTEM
-- (Isso é redundante se o script rodar antes de tudo, mas não causa mal)
GRANT SELECT ON ALL TABLES IN SCHEMA public TO grafana_reader;

-- 6. Permite que o Grafana leia os metadados do TimescaleDB (para otimização)
GRANT SELECT ON TABLE _timescaledb_catalog.hypertable TO grafana_reader;
GRANT SELECT ON TABLE _timescaledb_catalog.dimension TO grafana_reader;
GRANT SELECT ON TABLE timescaledb_information.hypertables TO grafana_reader;