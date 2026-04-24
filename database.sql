CREATE DATABASE IF NOT EXISTS sistema_bancario
  CHARACTER SET utf8mb4
  COLLATE utf8mb4_unicode_ci;

USE sistema_bancario;

CREATE TABLE IF NOT EXISTS clientes (
  id INT AUTO_INCREMENT PRIMARY KEY,
  nome VARCHAR(120) NOT NULL,
  cpf VARCHAR(14) NOT NULL UNIQUE,
  email VARCHAR(120) NOT NULL UNIQUE,
  senha_hash CHAR(64) NOT NULL,
  criado_em TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS contas (
  id INT AUTO_INCREMENT PRIMARY KEY,
  cliente_id INT NOT NULL UNIQUE,
  numero_conta VARCHAR(20) NOT NULL UNIQUE,
  saldo DECIMAL(12,2) NOT NULL DEFAULT 0.00,
  ativa TINYINT(1) NOT NULL DEFAULT 1,
  criado_em TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
  CONSTRAINT fk_conta_cliente FOREIGN KEY (cliente_id) REFERENCES clientes(id)
);

CREATE TABLE IF NOT EXISTS movimentacoes (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  conta_origem_id INT NULL,
  conta_destino_id INT NULL,
  tipo ENUM('DEPOSITO','SAQUE','TRANSFERENCIA') NOT NULL,
  valor DECIMAL(12,2) NOT NULL,
  descricao VARCHAR(255) NULL,
  criado_em TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
  CONSTRAINT fk_mov_origem FOREIGN KEY (conta_origem_id) REFERENCES contas(id),
  CONSTRAINT fk_mov_destino FOREIGN KEY (conta_destino_id) REFERENCES contas(id)
);

INSERT INTO clientes (nome, cpf, email, senha_hash)
SELECT 'Cliente Teste', '11111111111', 'teste@sistema.com', SHA2('123456', 256)
WHERE NOT EXISTS (
  SELECT 1 FROM clientes WHERE cpf = '11111111111'
);

INSERT INTO contas (cliente_id, numero_conta, saldo, ativa)
SELECT c.id, 'CC111111', 1000.00, 1
FROM clientes c
WHERE c.cpf = '11111111111'
  AND NOT EXISTS (
    SELECT 1 FROM contas WHERE cliente_id = c.id
  );
