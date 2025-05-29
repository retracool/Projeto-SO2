// estrutura.h
#ifndef ESTRUTURA_H
#define ESTRUTURA_H

#include <windows.h>
#include <tchar.h>

#define MAX_USERNAME        32
#define MAX_PALAVRA         64
#define MAX_JOGADORES       20
#define MAXLETRAS           12

// Nomes usados por todas as aplica��es
#define ARBITRO_MUTEX_NAME  TEXT("SO2_Arbitro")
#define MEMORIA_PARTILHADA_NAME  TEXT("SO2_LetrasVisiveis")
#define ARBITRO_PIPE_NAME   TEXT("\\\\.\\pipe\\SO2_ArbitroPipe")


// Tipos de mensagem para pipe
typedef enum {
    MSG_ENTRAR,      // pedido de entrada
    MSG_SAIR,        // pedido de sa�da
    MSG_PALAVRA,     // tentativa de palavra
    MSG_PONTUACAO,   // pedido de pontua��o
    MSG_JOGADORES,   // pedido de lista de jogadores
    MSG_INFO,        // aviso do �rbitro
    MSG_RESPOSTA,     // resposta geral
    MSG_SUCESSO,
    MSG_ERRO
} TipoMensagem;

// Estrutura de mensagem Unicode com TCHAR
typedef struct {
    TipoMensagem tipo;
    TCHAR        username[MAX_USERNAME];
    TCHAR        conteudo[MAX_PALAVRA];
    float          pontuacao;
} Mensagem;

typedef struct {
    TCHAR username[32];
    float pontuacao;
    BOOL ativo;
    DWORD lastActivity; // timestamp da última atividade
} Jogador;

// Estrutura de mem�ria partilhada
typedef struct {
    TCHAR letras[MAXLETRAS];
    int   estado[MAXLETRAS];
    TCHAR ultima_palavra[MAX_PALAVRA];
    Jogador jogadores[MAX_JOGADORES];
    int numJogadores;
    CRITICAL_SECTION csJogadores; // sincronização de acesso
} MemoriaPartilhada;

// Par�metros passados � thread que atende cada cliente
typedef struct {
    HANDLE             hPipe;  // handle do named pipe
    MemoriaPartilhada* mem;    // ponteiro para a mem�ria partilhada
} CLIENT_PARAM;
#endif // ESTRUTURA_H
