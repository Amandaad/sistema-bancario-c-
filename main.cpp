#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <mysql.h>

using namespace std;

static const string DB_HOST = "127.0.0.1";
static const string DB_USER = "root";
static const string DB_PASS = "";
static const string DB_NAME = "sistema_bancario";
static const unsigned int DB_PORT = 3306;

struct Sessao {
    int clienteId = 0;
    int contaId = 0;
    string nome;
    string numeroConta;
    bool logado = false;
};

string escapar(MYSQL* conn, const string& texto) {
    vector<char> buffer(texto.size() * 2 + 1);
    unsigned long tamanho = mysql_real_escape_string(
        conn, buffer.data(), texto.c_str(), static_cast<unsigned long>(texto.size()));
    return string(buffer.data(), tamanho);
}

bool executarSQL(MYSQL* conn, const string& sql) {
    if (mysql_query(conn, sql.c_str()) != 0) {
        cerr << "Erro SQL: " << mysql_error(conn) << "\n";
        return false;
    }
    return true;
}

bool iniciarTransacao(MYSQL* conn) {
    return executarSQL(conn, "START TRANSACTION");
}

bool commitTransacao(MYSQL* conn) {
    return executarSQL(conn, "COMMIT");
}

void rollbackTransacao(MYSQL* conn) {
    mysql_query(conn, "ROLLBACK");
}

string gerarNumeroConta() {
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<int> dist(100000, 999999);
    return "CC" + to_string(dist(gen));
}

bool numeroContaExiste(MYSQL* conn, const string& numeroConta) {
    string sql = "SELECT id FROM contas WHERE numero_conta='" + escapar(conn, numeroConta) + "' LIMIT 1";
    if (!executarSQL(conn, sql)) {
        return true;
    }
    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) {
        return true;
    }
    bool existe = mysql_num_rows(res) > 0;
    mysql_free_result(res);
    return existe;
}

string gerarNumeroContaUnico(MYSQL* conn) {
    string numero;
    do {
        numero = gerarNumeroConta();
    } while (numeroContaExiste(conn, numero));
    return numero;
}

double lerValorPositivo(const string& prompt) {
    double valor = 0;
    while (true) {
        cout << prompt;
        cin >> valor;
        if (!cin.fail() && valor > 0) {
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            return valor;
        }
        cout << "Valor invalido. Tente novamente.\n";
        cin.clear();
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
    }
}

bool cadastrarCliente(MYSQL* conn) {
    string nome, cpf, email, senha;
    cout << "\n=== Cadastro de Cliente ===\n";
    cout << "Nome: ";
    getline(cin, nome);
    cout << "CPF (somente numeros): ";
    getline(cin, cpf);
    cout << "Email: ";
    getline(cin, email);
    cout << "Senha: ";
    getline(cin, senha);

    if (nome.empty() || cpf.empty() || email.empty() || senha.empty()) {
        cout << "Todos os campos sao obrigatorios.\n";
        return false;
    }

    if (!iniciarTransacao(conn)) {
        return false;
    }

    string sqlCliente =
        "INSERT INTO clientes (nome, cpf, email, senha_hash) VALUES ('" + escapar(conn, nome) + "', '" +
        escapar(conn, cpf) + "', '" + escapar(conn, email) + "', SHA2('" + escapar(conn, senha) + "', 256))";

    if (!executarSQL(conn, sqlCliente)) {
        rollbackTransacao(conn);
        return false;
    }

    long long clienteId = mysql_insert_id(conn);
    string numeroConta = gerarNumeroContaUnico(conn);
    ostringstream sqlConta;
    sqlConta << "INSERT INTO contas (cliente_id, numero_conta, saldo) VALUES (" << clienteId << ", '"
             << escapar(conn, numeroConta) << "', 0.00)";

    if (!executarSQL(conn, sqlConta.str())) {
        rollbackTransacao(conn);
        return false;
    }

    if (!commitTransacao(conn)) {
        rollbackTransacao(conn);
        return false;
    }

    cout << "Cliente cadastrado com sucesso.\n";
    cout << "Numero da conta: " << numeroConta << "\n";
    return true;
}

bool login(MYSQL* conn, Sessao& sessao) {
    string cpf, senha;
    cout << "\n=== Login ===\n";
    cout << "CPF: ";
    getline(cin, cpf);
    cout << "Senha: ";
    getline(cin, senha);

    string sql =
        "SELECT c.id, ct.id, c.nome, ct.numero_conta "
        "FROM clientes c "
        "INNER JOIN contas ct ON ct.cliente_id = c.id "
        "WHERE c.cpf='" +
        escapar(conn, cpf) + "' AND c.senha_hash=SHA2('" + escapar(conn, senha) + "', 256) AND ct.ativa=1 LIMIT 1";

    if (!executarSQL(conn, sql)) {
        return false;
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) {
        cerr << "Erro ao obter resultado do login.\n";
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        mysql_free_result(res);
        cout << "CPF ou senha invalidos.\n";
        return false;
    }

    sessao.clienteId = stoi(row[0]);
    sessao.contaId = stoi(row[1]);
    sessao.nome = row[2] ? row[2] : "";
    sessao.numeroConta = row[3] ? row[3] : "";
    sessao.logado = true;
    mysql_free_result(res);

    cout << "Login realizado com sucesso. Bem-vindo(a), " << sessao.nome << ".\n";
    return true;
}

bool mostrarSaldo(MYSQL* conn, const Sessao& sessao) {
    ostringstream sql;
    sql << "SELECT saldo FROM contas WHERE id=" << sessao.contaId << " LIMIT 1";
    if (!executarSQL(conn, sql.str())) {
        return false;
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) {
        return false;
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        mysql_free_result(res);
        cout << "Conta nao encontrada.\n";
        return false;
    }

    cout << fixed << setprecision(2);
    cout << "Saldo atual: R$ " << (row[0] ? row[0] : "0.00") << "\n";
    mysql_free_result(res);
    return true;
}

bool depositar(MYSQL* conn, const Sessao& sessao) {
    double valor = lerValorPositivo("Valor do deposito: R$ ");

    if (!iniciarTransacao(conn)) {
        return false;
    }

    ostringstream sqlUpdate;
    sqlUpdate << "UPDATE contas SET saldo = saldo + " << fixed << setprecision(2) << valor << " WHERE id="
              << sessao.contaId;
    if (!executarSQL(conn, sqlUpdate.str())) {
        rollbackTransacao(conn);
        return false;
    }

    ostringstream sqlMov;
    sqlMov << "INSERT INTO movimentacoes (conta_destino_id, tipo, valor, descricao) VALUES (" << sessao.contaId
           << ", 'DEPOSITO', " << fixed << setprecision(2) << valor << ", 'Deposito em conta')";
    if (!executarSQL(conn, sqlMov.str())) {
        rollbackTransacao(conn);
        return false;
    }

    if (!commitTransacao(conn)) {
        rollbackTransacao(conn);
        return false;
    }

    cout << "Deposito realizado com sucesso.\n";
    return true;
}

bool sacar(MYSQL* conn, const Sessao& sessao) {
    double valor = lerValorPositivo("Valor do saque: R$ ");

    if (!iniciarTransacao(conn)) {
        return false;
    }

    ostringstream sqlSaldo;
    sqlSaldo << "SELECT saldo FROM contas WHERE id=" << sessao.contaId << " FOR UPDATE";
    if (!executarSQL(conn, sqlSaldo.str())) {
        rollbackTransacao(conn);
        return false;
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) {
        rollbackTransacao(conn);
        return false;
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        mysql_free_result(res);
        rollbackTransacao(conn);
        cout << "Conta nao encontrada.\n";
        return false;
    }

    double saldoAtual = stod(row[0]);
    mysql_free_result(res);

    if (saldoAtual < valor) {
        rollbackTransacao(conn);
        cout << "Saldo insuficiente.\n";
        return false;
    }

    ostringstream sqlUpdate;
    sqlUpdate << "UPDATE contas SET saldo = saldo - " << fixed << setprecision(2) << valor << " WHERE id="
              << sessao.contaId;
    if (!executarSQL(conn, sqlUpdate.str())) {
        rollbackTransacao(conn);
        return false;
    }

    ostringstream sqlMov;
    sqlMov << "INSERT INTO movimentacoes (conta_origem_id, tipo, valor, descricao) VALUES (" << sessao.contaId
           << ", 'SAQUE', " << fixed << setprecision(2) << valor << ", 'Saque em conta')";
    if (!executarSQL(conn, sqlMov.str())) {
        rollbackTransacao(conn);
        return false;
    }

    if (!commitTransacao(conn)) {
        rollbackTransacao(conn);
        return false;
    }

    cout << "Saque realizado com sucesso.\n";
    return true;
}

bool transferir(MYSQL* conn, const Sessao& sessao) {
    string numeroDestino;
    cout << "Numero da conta destino: ";
    getline(cin, numeroDestino);
    if (numeroDestino.empty()) {
        cout << "Conta destino invalida.\n";
        return false;
    }

    double valor = lerValorPositivo("Valor da transferencia: R$ ");

    ostringstream sqlContaDestino;
    sqlContaDestino << "SELECT id FROM contas WHERE numero_conta='" << escapar(conn, numeroDestino)
                    << "' AND ativa=1 LIMIT 1";
    if (!executarSQL(conn, sqlContaDestino.str())) {
        return false;
    }
    MYSQL_RES* resDestino = mysql_store_result(conn);
    if (!resDestino) {
        return false;
    }
    MYSQL_ROW rowDestino = mysql_fetch_row(resDestino);
    if (!rowDestino) {
        mysql_free_result(resDestino);
        cout << "Conta destino nao encontrada.\n";
        return false;
    }
    int contaDestinoId = stoi(rowDestino[0]);
    mysql_free_result(resDestino);

    if (contaDestinoId == sessao.contaId) {
        cout << "Nao e possivel transferir para a mesma conta.\n";
        return false;
    }

    if (!iniciarTransacao(conn)) {
        return false;
    }

    int menorId = min(sessao.contaId, contaDestinoId);
    int maiorId = max(sessao.contaId, contaDestinoId);

    ostringstream sqlLock;
    sqlLock << "SELECT id, saldo FROM contas WHERE id IN (" << menorId << ", " << maiorId << ") FOR UPDATE";
    if (!executarSQL(conn, sqlLock.str())) {
        rollbackTransacao(conn);
        return false;
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) {
        rollbackTransacao(conn);
        return false;
    }

    double saldoOrigem = -1.0;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr) {
        int id = stoi(row[0]);
        double saldo = stod(row[1]);
        if (id == sessao.contaId) {
            saldoOrigem = saldo;
        }
    }
    mysql_free_result(res);

    if (saldoOrigem < valor || saldoOrigem < 0) {
        rollbackTransacao(conn);
        cout << "Saldo insuficiente para transferencia.\n";
        return false;
    }

    ostringstream sqlDebito;
    sqlDebito << "UPDATE contas SET saldo = saldo - " << fixed << setprecision(2) << valor << " WHERE id="
              << sessao.contaId;
    if (!executarSQL(conn, sqlDebito.str())) {
        rollbackTransacao(conn);
        return false;
    }

    ostringstream sqlCredito;
    sqlCredito << "UPDATE contas SET saldo = saldo + " << fixed << setprecision(2) << valor << " WHERE id="
               << contaDestinoId;
    if (!executarSQL(conn, sqlCredito.str())) {
        rollbackTransacao(conn);
        return false;
    }

    ostringstream sqlMov;
    sqlMov << "INSERT INTO movimentacoes (conta_origem_id, conta_destino_id, tipo, valor, descricao) VALUES ("
           << sessao.contaId << ", " << contaDestinoId << ", 'TRANSFERENCIA', " << fixed << setprecision(2) << valor
           << ", 'Transferencia entre contas')";
    if (!executarSQL(conn, sqlMov.str())) {
        rollbackTransacao(conn);
        return false;
    }

    if (!commitTransacao(conn)) {
        rollbackTransacao(conn);
        return false;
    }

    cout << "Transferencia realizada com sucesso.\n";
    return true;
}

bool listarHistorico(MYSQL* conn, const Sessao& sessao) {
    ostringstream sql;
    sql << "SELECT tipo, valor, conta_origem_id, conta_destino_id, descricao, criado_em "
           "FROM movimentacoes "
           "WHERE conta_origem_id="
        << sessao.contaId << " OR conta_destino_id=" << sessao.contaId
        << " ORDER BY id DESC LIMIT 50";

    if (!executarSQL(conn, sql.str())) {
        return false;
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) {
        return false;
    }

    cout << "\n=== Historico de Movimentacoes ===\n";
    if (mysql_num_rows(res) == 0) {
        cout << "Nenhuma movimentacao encontrada.\n";
        mysql_free_result(res);
        return true;
    }

    cout << fixed << setprecision(2);
    MYSQL_ROW row;
    int i = 1;
    while ((row = mysql_fetch_row(res)) != nullptr) {
        string tipo = row[0] ? row[0] : "";
        string valor = row[1] ? row[1] : "0.00";
        int origemId = row[2] ? stoi(row[2]) : 0;
        int destinoId = row[3] ? stoi(row[3]) : 0;
        string descricao = row[4] ? row[4] : "";
        string data = row[5] ? row[5] : "";

        string sentido;
        if (tipo == "DEPOSITO") {
            sentido = "ENTRADA";
        } else if (tipo == "SAQUE") {
            sentido = "SAIDA";
        } else if (origemId == sessao.contaId) {
            sentido = "SAIDA";
        } else if (destinoId == sessao.contaId) {
            sentido = "ENTRADA";
        } else {
            sentido = "-";
        }

        cout << i++ << ". [" << data << "] " << tipo << " | " << sentido << " | R$ " << valor << " | " << descricao
             << "\n";
    }
    mysql_free_result(res);
    return true;
}

void menuLogado(MYSQL* conn, Sessao& sessao) {
    int opcao = -1;
    while (opcao != 0) {
        cout << "\n=== Menu da Conta (" << sessao.numeroConta << ") ===\n";
        cout << "1. Ver saldo\n";
        cout << "2. Deposito\n";
        cout << "3. Saque\n";
        cout << "4. Transferencia\n";
        cout << "5. Historico de movimentacoes\n";
        cout << "0. Logout\n";
        cout << "Escolha: ";
        cin >> opcao;
        if (cin.fail()) {
            cin.clear();
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            opcao = -1;
            cout << "Opcao invalida.\n";
            continue;
        }
        cin.ignore(numeric_limits<streamsize>::max(), '\n');

        switch (opcao) {
        case 1:
            mostrarSaldo(conn, sessao);
            break;
        case 2:
            depositar(conn, sessao);
            break;
        case 3:
            sacar(conn, sessao);
            break;
        case 4:
            transferir(conn, sessao);
            break;
        case 5:
            listarHistorico(conn, sessao);
            break;
        case 0:
            sessao = Sessao{};
            cout << "Logout realizado.\n";
            break;
        default:
            cout << "Opcao invalida.\n";
            break;
        }
    }
}

int main() {
    MYSQL* conn = mysql_init(nullptr);
    if (!conn) {
        cerr << "Falha ao inicializar MySQL.\n";
        return 1;
    }

    if (!mysql_real_connect(conn, DB_HOST.c_str(), DB_USER.c_str(), DB_PASS.c_str(), DB_NAME.c_str(), DB_PORT, nullptr,
                            0)) {
        cerr << "Erro na conexao com MySQL: " << mysql_error(conn) << "\n";
        mysql_close(conn);
        return 1;
    }

    mysql_set_character_set(conn, "utf8mb4");

    cout << "=====================================\n";
    cout << "  Sistema Bancario C++ + MySQL\n";
    cout << "=====================================\n";

    int opcao = -1;
    while (opcao != 0) {
        cout << "\n=== Menu Principal ===\n";
        cout << "1. Cadastro de cliente\n";
        cout << "2. Login\n";
        cout << "0. Sair\n";
        cout << "Escolha: ";
        cin >> opcao;
        if (cin.fail()) {
            cin.clear();
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            opcao = -1;
            cout << "Opcao invalida.\n";
            continue;
        }
        cin.ignore(numeric_limits<streamsize>::max(), '\n');

        if (opcao == 1) {
            cadastrarCliente(conn);
        } else if (opcao == 2) {
            Sessao sessao;
            if (login(conn, sessao)) {
                menuLogado(conn, sessao);
            }
        } else if (opcao == 0) {
            cout << "Encerrando sistema.\n";
        } else {
            cout << "Opcao invalida.\n";
        }
    }

    mysql_close(conn);
    return 0;
}
