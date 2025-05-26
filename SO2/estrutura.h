// estrutura.h
#ifndef ESTRUTURA_H
#define ESTRUTURA_H

#include <windows.h>

#define MAX_USERNAME        32
#define MAX_PALAVRA         64
#define MAX_JOGADORES       20
#define MAXLETRAS           12

// Nomes usados por todas as aplicações
#define ARBITRO_MUTEX_NAME  TEXT("SO2_Arbitro")
#define MEMORIA_PARTILHADA_NAME  TEXT("SO2_LetrasVisiveis")
#define ARBITRO_PIPE_NAME   TEXT("\\\\.\\pipe\\SO2_ArbitroPipe")


// Tipos de mensagem para pipe
typedef enum {
    MSG_ENTRAR,      // pedido de entrada
    MSG_SAIR,        // pedido de saída
    MSG_PALAVRA,     // tentativa de palavra
    MSG_PONTUACAO,   // pedido de pontuação
    MSG_JOGADORES,   // pedido de lista de jogadores
    MSG_INFO,        // aviso do árbitro
    MSG_RESPOSTA     // resposta geral
} TipoMensagem;

// Formato de cada mensagem trocada por pipe
typedef struct {
    TipoMensagem tipo;
    char         username[MAX_USERNAME];
    char         conteudo[MAX_PALAVRA];
    int          pontuacao;  // usado em respostas
} Mensagem;

// Layout da memória partilhada
typedef struct {
    char letras[MAXLETRAS];            // vetor de letras visíveis ('_' = vazio)
    int  estado[MAXLETRAS];            // 0=vazio, 1=ocupado (opcional)
    char ultima_palavra[MAX_PALAVRA];  // última palavra correta
} MemoriaPartilhada;

#endif // ESTRUTURA_H
