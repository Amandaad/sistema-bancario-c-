# Sistema Bancario (C++ + MySQL/XAMPP)

Projeto de console com:

- cadastro de clientes
- login
- consulta de saldo
- deposito
- saque
- transferencia
- historico de movimentacoes

## 1) Criar banco no phpMyAdmin

1. Inicie `Apache` e `MySQL` no XAMPP.
2. Abra `http://localhost/phpmyadmin`.
3. Clique em `SQL` e execute o arquivo `database.sql`.

## 2) Ajustar credenciais do banco

No `main.cpp`, ajuste se necessario:

- `DB_HOST`
- `DB_USER`
- `DB_PASS`
- `DB_NAME`
- `DB_PORT`

Por padrao no XAMPP:

- host: `127.0.0.1`
- usuario: `root`
- senha: vazia
- banco: `sistema_bancario`
- porta: `3306`

## 3) Compilar no Windows (MinGW)

Exemplo (ajuste caminhos da sua instalacao):

```powershell
g++ .\main.cpp -o .\sistema_bancario.exe -IC:\xampp\mysql\include -LC:\xampp\mysql\lib -llibmysql
```

Se der erro de biblioteca na execucao, copie `libmysql.dll` para a mesma pasta do `.exe` ou adicione `C:\xampp\mysql\bin` no `PATH`.

## 4) Executar

```powershell
.\sistema_bancario.exe
```

## Observacoes tecnicas

- Senha e validada no banco usando `SHA2(..., 256)`.
- Saque e transferencia usam transacao para evitar inconsistencias de saldo.
- Historico mostra as ultimas 50 movimentacoes da conta logada.
