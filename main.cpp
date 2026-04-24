#include <algorithm>
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

class Database {
  private:
    MYSQL* conn_ = nullptr;

  public:
    Database(const string& host, const string& user, const string& pass, const string& dbName, unsigned int port) {
        conn_ = mysql_init(nullptr);
        if (!conn_) {
            cerr << "Falha ao inicializar MySQL.\n";
            return;
        }

        if (!mysql_real_connect(conn_, host.c_str(), user.c_str(), pass.c_str(), dbName.c_str(), port, nullptr, 0)) {
            cerr << "Erro na conexao com MySQL: " << mysql_error(conn_) << "\n";
            mysql_close(conn_);
            conn_ = nullptr;
            return;
        }

        mysql_set_character_set(conn_, "utf8mb4");
    }

    ~Database() {
        if (conn_) {
            mysql_close(conn_);
            conn_ = nullptr;
        }
    }

    bool conectado() const { return conn_ != nullptr; }

    string escapar(const string& texto) const {
        vector<char> buffer(texto.size() * 2 + 1);
        unsigned long tamanho = mysql_real_escape_string(conn_, buffer.data(), texto.c_str(),
                                                         static_cast<unsigned long>(texto.size()));
        return string(buffer.data(), tamanho);
    }

    bool executar(const string& sql) const {
        if (mysql_query(conn_, sql.c_str()) != 0) {
            cerr << "Erro SQL: " << mysql_error(conn_) << "\n";
            return false;
        }
        return true;
    }

    MYSQL_RES* consultar(const string& sql) const {
        if (!executar(sql)) {
            return nullptr;
        }
        return mysql_store_result(conn_);
    }

    long long ultimoIdInserido() const { return mysql_insert_id(conn_); }

    bool iniciarTransacao() const { return executar("START TRANSACTION"); }

    bool commit() const { return executar("COMMIT"); }

    void rollback() const { mysql_query(conn_, "ROLLBACK"); }
};

class Conta {
  private:
    int id_ = 0;
    int clienteId_ = 0;
    string numeroConta_;
    double saldo_ = 0.0;

    static string gerarNumeroConta() {
        random_device rd;
        mt19937 gen(rd());
        uniform_int_distribution<int> dist(100000, 999999);
        return "CC" + to_string(dist(gen));
    }

    static bool numeroContaExiste(const Database& db, const string& numeroConta) {
        string sql = "SELECT id FROM contas WHERE numero_conta='" + db.escapar(numeroConta) + "' LIMIT 1";
        MYSQL_RES* res = db.consultar(sql);
        if (!res) {
            return true;
        }
        bool existe = mysql_num_rows(res) > 0;
        mysql_free_result(res);
        return existe;
    }

  public:
    Conta() = default;
    Conta(int id, int clienteId, const string& numeroConta, double saldo)
        : id_(id), clienteId_(clienteId), numeroConta_(numeroConta), saldo_(saldo) {}

    int id() const { return id_; }
    int clienteId() const { return clienteId_; }
    const string& numeroConta() const { return numeroConta_; }
    double saldo() const { return saldo_; }

    static string gerarNumeroContaUnico(const Database& db) {
        string numero;
        do {
            numero = gerarNumeroConta();
        } while (numeroContaExiste(db, numero));
        return numero;
    }

    static bool buscarPorNumero(const Database& db, const string& numeroConta, Conta& conta) {
        string sql = "SELECT id, cliente_id, numero_conta, saldo FROM contas WHERE numero_conta='" +
                     db.escapar(numeroConta) + "' AND ativa=1 LIMIT 1";
        MYSQL_RES* res = db.consultar(sql);
        if (!res) {
            return false;
        }

        MYSQL_ROW row = mysql_fetch_row(res);
        if (!row) {
            mysql_free_result(res);
            return false;
        }

        conta = Conta(stoi(row[0]), stoi(row[1]), row[2] ? row[2] : "", row[3] ? stod(row[3]) : 0.0);
        mysql_free_result(res);
        return true;
    }

    bool atualizarSaldo(const Database& db) {
        ostringstream sql;
        sql << "SELECT saldo FROM contas WHERE id=" << id_ << " LIMIT 1";
        MYSQL_RES* res = db.consultar(sql.str());
        if (!res) {
            return false;
        }
        MYSQL_ROW row = mysql_fetch_row(res);
        if (!row) {
            mysql_free_result(res);
            return false;
        }
        saldo_ = row[0] ? stod(row[0]) : 0.0;
        mysql_free_result(res);
        return true;
    }

    bool depositar(const Database& db, double valor) {
        if (!db.iniciarTransacao()) {
            return false;
        }

        ostringstream sqlUpdate;
        sqlUpdate << "UPDATE contas SET saldo = saldo + " << fixed << setprecision(2) << valor << " WHERE id=" << id_;
        if (!db.executar(sqlUpdate.str())) {
            db.rollback();
            return false;
        }

        ostringstream sqlMov;
        sqlMov << "INSERT INTO movimentacoes (conta_destino_id, tipo, valor, descricao) VALUES (" << id_
               << ", 'DEPOSITO', " << fixed << setprecision(2) << valor << ", 'Deposito em conta')";
        if (!db.executar(sqlMov.str())) {
            db.rollback();
            return false;
        }

        if (!db.commit()) {
            db.rollback();
            return false;
        }

        saldo_ += valor;
        return true;
    }

    bool sacar(const Database& db, double valor) {
        if (!db.iniciarTransacao()) {
            return false;
        }

        ostringstream sqlSaldo;
        sqlSaldo << "SELECT saldo FROM contas WHERE id=" << id_ << " FOR UPDATE";
        MYSQL_RES* res = db.consultar(sqlSaldo.str());
        if (!res) {
            db.rollback();
            return false;
        }

        MYSQL_ROW row = mysql_fetch_row(res);
        if (!row) {
            mysql_free_result(res);
            db.rollback();
            cout << "Conta nao encontrada.\n";
            return false;
        }

        double saldoAtual = row[0] ? stod(row[0]) : 0.0;
        mysql_free_result(res);

        if (saldoAtual < valor) {
            db.rollback();
            cout << "Saldo insuficiente.\n";
            return false;
        }

        ostringstream sqlUpdate;
        sqlUpdate << "UPDATE contas SET saldo = saldo - " << fixed << setprecision(2) << valor << " WHERE id=" << id_;
        if (!db.executar(sqlUpdate.str())) {
            db.rollback();
            return false;
        }

        ostringstream sqlMov;
        sqlMov << "INSERT INTO movimentacoes (conta_origem_id, tipo, valor, descricao) VALUES (" << id_
               << ", 'SAQUE', " << fixed << setprecision(2) << valor << ", 'Saque em conta')";
        if (!db.executar(sqlMov.str())) {
            db.rollback();
            return false;
        }

        if (!db.commit()) {
            db.rollback();
            return false;
        }

        saldo_ = saldoAtual - valor;
        return true;
    }

    bool transferir(const Database& db, const Conta& destino, double valor) {
        if (destino.id_ == id_) {
            cout << "Nao e possivel transferir para a mesma conta.\n";
            return false;
        }

        if (!db.iniciarTransacao()) {
            return false;
        }

        int menorId = min(id_, destino.id_);
        int maiorId = max(id_, destino.id_);

        ostringstream sqlLock;
        sqlLock << "SELECT id, saldo FROM contas WHERE id IN (" << menorId << ", " << maiorId << ") FOR UPDATE";
        MYSQL_RES* res = db.consultar(sqlLock.str());
        if (!res) {
            db.rollback();
            return false;
        }

        double saldoOrigem = -1.0;
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res)) != nullptr) {
            int contaId = stoi(row[0]);
            double saldo = row[1] ? stod(row[1]) : 0.0;
            if (contaId == id_) {
                saldoOrigem = saldo;
            }
        }
        mysql_free_result(res);

        if (saldoOrigem < valor || saldoOrigem < 0) {
            db.rollback();
            cout << "Saldo insuficiente para transferencia.\n";
            return false;
        }

        ostringstream sqlDebito;
        sqlDebito << "UPDATE contas SET saldo = saldo - " << fixed << setprecision(2) << valor << " WHERE id=" << id_;
        if (!db.executar(sqlDebito.str())) {
            db.rollback();
            return false;
        }

        ostringstream sqlCredito;
        sqlCredito << "UPDATE contas SET saldo = saldo + " << fixed << setprecision(2) << valor << " WHERE id="
                   << destino.id_;
        if (!db.executar(sqlCredito.str())) {
            db.rollback();
            return false;
        }

        ostringstream sqlMov;
        sqlMov << "INSERT INTO movimentacoes (conta_origem_id, conta_destino_id, tipo, valor, descricao) VALUES ("
               << id_ << ", " << destino.id_ << ", 'TRANSFERENCIA', " << fixed << setprecision(2) << valor
               << ", 'Transferencia entre contas')";
        if (!db.executar(sqlMov.str())) {
            db.rollback();
            return false;
        }

        if (!db.commit()) {
            db.rollback();
            return false;
        }

        saldo_ = saldoOrigem - valor;
        return true;
    }

    bool listarHistorico(const Database& db) const {
        ostringstream sql;
        sql << "SELECT tipo, valor, conta_origem_id, conta_destino_id, descricao, criado_em "
               "FROM movimentacoes WHERE conta_origem_id="
            << id_ << " OR conta_destino_id=" << id_ << " ORDER BY id DESC LIMIT 50";

        MYSQL_RES* res = db.consultar(sql.str());
        if (!res) {
            return false;
        }

        cout << "\n=== Historico de Movimentacoes ===\n";
        if (mysql_num_rows(res) == 0) {
            cout << "Nenhuma movimentacao encontrada.\n";
            mysql_free_result(res);
            return true;
        }

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
            } else if (origemId == id_) {
                sentido = "SAIDA";
            } else if (destinoId == id_) {
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
};

class Cliente {
  private:
    int id_ = 0;
    string nome_;
    string cpf_;
    string email_;

  public:
    Cliente() = default;
    Cliente(int id, const string& nome, const string& cpf, const string& email)
        : id_(id), nome_(nome), cpf_(cpf), email_(email) {}

    int id() const { return id_; }
    const string& nome() const { return nome_; }

    static bool cadastrar(const Database& db, const string& nome, const string& cpf, const string& email,
                          const string& senha, Cliente& clienteCriado, Conta& contaCriada) {
        if (!db.iniciarTransacao()) {
            return false;
        }

        string sqlCliente = "INSERT INTO clientes (nome, cpf, email, senha_hash) VALUES ('" + db.escapar(nome) + "', '" +
                            db.escapar(cpf) + "', '" + db.escapar(email) + "', SHA2('" + db.escapar(senha) + "', 256))";

        if (!db.executar(sqlCliente)) {
            db.rollback();
            return false;
        }

        int clienteId = static_cast<int>(db.ultimoIdInserido());
        string numeroConta = Conta::gerarNumeroContaUnico(db);

        ostringstream sqlConta;
        sqlConta << "INSERT INTO contas (cliente_id, numero_conta, saldo) VALUES (" << clienteId << ", '"
                 << db.escapar(numeroConta) << "', 0.00)";
        if (!db.executar(sqlConta.str())) {
            db.rollback();
            return false;
        }

        if (!db.commit()) {
            db.rollback();
            return false;
        }

        clienteCriado = Cliente(clienteId, nome, cpf, email);
        contaCriada = Conta(0, clienteId, numeroConta, 0.0);
        return true;
    }

    static bool autenticar(const Database& db, const string& cpf, const string& senha, Cliente& cliente, Conta& conta) {
        string sql =
            "SELECT c.id, c.nome, c.cpf, c.email, ct.id, ct.numero_conta, ct.saldo "
            "FROM clientes c INNER JOIN contas ct ON ct.cliente_id = c.id "
            "WHERE c.cpf='" +
            db.escapar(cpf) + "' AND c.senha_hash=SHA2('" + db.escapar(senha) + "', 256) AND ct.ativa=1 LIMIT 1";

        MYSQL_RES* res = db.consultar(sql);
        if (!res) {
            return false;
        }

        MYSQL_ROW row = mysql_fetch_row(res);
        if (!row) {
            mysql_free_result(res);
            return false;
        }

        cliente = Cliente(stoi(row[0]), row[1] ? row[1] : "", row[2] ? row[2] : "", row[3] ? row[3] : "");
        conta = Conta(stoi(row[4]), cliente.id(), row[5] ? row[5] : "", row[6] ? stod(row[6]) : 0.0);
        mysql_free_result(res);
        return true;
    }
};

class Banco {
  private:
    Database db_;
    bool logado_ = false;
    Cliente clienteLogado_;
    Conta contaLogada_;

    static double lerValorPositivo(const string& prompt) {
        double valor = 0.0;
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

    void cadastrarCliente() {
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
            return;
        }

        Cliente clienteCriado;
        Conta contaCriada;
        if (!Cliente::cadastrar(db_, nome, cpf, email, senha, clienteCriado, contaCriada)) {
            cout << "Falha no cadastro.\n";
            return;
        }

        cout << "Cliente cadastrado com sucesso.\n";
        cout << "Numero da conta: " << contaCriada.numeroConta() << "\n";
    }

    void login() {
        string cpf, senha;
        cout << "\n=== Login ===\n";
        cout << "CPF: ";
        getline(cin, cpf);
        cout << "Senha: ";
        getline(cin, senha);

        Cliente cliente;
        Conta conta;
        if (!Cliente::autenticar(db_, cpf, senha, cliente, conta)) {
            cout << "CPF ou senha invalidos.\n";
            return;
        }

        clienteLogado_ = cliente;
        contaLogada_ = conta;
        logado_ = true;
        cout << "Login realizado com sucesso. Bem-vindo(a), " << clienteLogado_.nome() << ".\n";
    }

    void mostrarSaldo() {
        if (!contaLogada_.atualizarSaldo(db_)) {
            cout << "Nao foi possivel consultar saldo.\n";
            return;
        }
        cout << fixed << setprecision(2);
        cout << "Saldo atual: R$ " << contaLogada_.saldo() << "\n";
    }

    void depositar() {
        double valor = lerValorPositivo("Valor do deposito: R$ ");
        if (contaLogada_.depositar(db_, valor)) {
            cout << "Deposito realizado com sucesso.\n";
            return;
        }
        cout << "Falha ao realizar deposito.\n";
    }

    void sacar() {
        double valor = lerValorPositivo("Valor do saque: R$ ");
        if (contaLogada_.sacar(db_, valor)) {
            cout << "Saque realizado com sucesso.\n";
            return;
        }
        cout << "Falha ao realizar saque.\n";
    }

    void transferir() {
        string numeroDestino;
        cout << "Numero da conta destino: ";
        getline(cin, numeroDestino);
        if (numeroDestino.empty()) {
            cout << "Conta destino invalida.\n";
            return;
        }

        Conta contaDestino;
        if (!Conta::buscarPorNumero(db_, numeroDestino, contaDestino)) {
            cout << "Conta destino nao encontrada.\n";
            return;
        }

        double valor = lerValorPositivo("Valor da transferencia: R$ ");
        if (contaLogada_.transferir(db_, contaDestino, valor)) {
            cout << "Transferencia realizada com sucesso.\n";
            return;
        }
        cout << "Falha ao realizar transferencia.\n";
    }

    void historico() { contaLogada_.listarHistorico(db_); }

    void menuConta() {
        int opcao = -1;
        while (opcao != 0 && logado_) {
            cout << "\n=== Menu da Conta (" << contaLogada_.numeroConta() << ") ===\n";
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
                mostrarSaldo();
                break;
            case 2:
                depositar();
                break;
            case 3:
                sacar();
                break;
            case 4:
                transferir();
                break;
            case 5:
                historico();
                break;
            case 0:
                logado_ = false;
                clienteLogado_ = Cliente();
                contaLogada_ = Conta();
                cout << "Logout realizado.\n";
                break;
            default:
                cout << "Opcao invalida.\n";
                break;
            }
        }
    }

  public:
    Banco() : db_(DB_HOST, DB_USER, DB_PASS, DB_NAME, DB_PORT) {}

    void executar() {
        if (!db_.conectado()) {
            return;
        }

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
                cadastrarCliente();
            } else if (opcao == 2) {
                login();
                if (logado_) {
                    menuConta();
                }
            } else if (opcao == 0) {
                cout << "Encerrando sistema.\n";
            } else {
                cout << "Opcao invalida.\n";
            }
        }
    }
};

int main() {
    Banco banco;
    banco.executar();
    return 0;
}
