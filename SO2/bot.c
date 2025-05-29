#define _CRT_SECURE_NO_WARNINGS
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <io.h>
#include "estrutura.h"

#define MAX_WORDS 5005
#define MAX_WORD_LENGTH 50

// Dicionário do bot
static TCHAR dictionary[MAX_WORDS][MAX_WORD_LENGTH];
static int dictionarySize = 0;

// Variáveis globais do bot
static TCHAR g_username[MAX_USERNAME];
static int g_tempoReacao = 10; // em segundos
static HANDLE g_hPipe = INVALID_HANDLE_VALUE;
static float g_pontuacao = 0;

// Função para carregar dicionário
BOOL LoadBotDictionary(const TCHAR *filename)
{
    FILE *file;
#ifdef UNICODE
    file = _wfopen(filename, L"r, ccs=UTF-8");
#else
    file = fopen(filename, "r");
#endif

    if (!file)
    {
        _tprintf(TEXT("[BOT] Erro ao carregar dicionário: %s\n"), filename);
        return FALSE;
    }

    dictionarySize = 0;
    TCHAR line[MAX_WORD_LENGTH];

    while (_fgetts(line, MAX_WORD_LENGTH, file) && dictionarySize < MAX_WORDS)
    {
        // Remove quebra de linha
        size_t len = _tcslen(line);
        if (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        {
            line[len - 1] = '\0';
            len--;
        }
        if (len > 0 && (line[len - 1] == '\r'))
        {
            line[len - 1] = '\0';
        }

        // Converte para maiúsculas
        for (int i = 0; line[i]; i++)
        {
            line[i] = _totupper(line[i]);
        }

        // Copia para o dicionário
        _tcsncpy(dictionary[dictionarySize], line, MAX_WORD_LENGTH - 1);
        dictionary[dictionarySize][MAX_WORD_LENGTH - 1] = '\0';
        dictionarySize++;
    }

    fclose(file);
    _tprintf(TEXT("[BOT] Dicionário carregado: %d palavras\n"), dictionarySize);
    return TRUE;
}

// Função para verificar se uma palavra pode ser formada com as letras disponíveis
BOOL PodeFormarPalavra(const TCHAR *palavra, MemoriaPartilhada *mem)
{
    TCHAR letrasTmp[MAXLETRAS];
    memcpy(letrasTmp, mem->letras, MAXLETRAS * sizeof(TCHAR));

    for (int i = 0; palavra[i] != '\0'; i++)
    {
        BOOL encontrou = FALSE;
        TCHAR letraProcurada = _totupper(palavra[i]);

        for (int j = 0; j < MAXLETRAS; j++)
        {
            if (mem->estado[j] == 1 && letraProcurada == _totupper(letrasTmp[j]))
            {
                letrasTmp[j] = '_'; // marca como usada
                encontrou = TRUE;
                break;
            }
        }
        if (!encontrou)
        {
            return FALSE;
        }
    }
    return TRUE;
}

// Função para gerar uma palavra aleatória com as letras disponíveis
TCHAR* GerarPalavraAleatoria(MemoriaPartilhada *mem)
{
    static TCHAR palavraAleatoria[MAX_WORD_LENGTH];
    TCHAR letrasDisponiveis[MAXLETRAS];
    int numLetrasDisponiveis = 0;
    
    // Apanha letras disponíveis
    for (int i = 0; i < MAXLETRAS; i++)
    {
        if (mem->estado[i] == 1)
        {
            letrasDisponiveis[numLetrasDisponiveis] = mem->letras[i];
            numLetrasDisponiveis++;
        }
    }
    
    if (numLetrasDisponiveis == 0)
    {
        return NULL;
    }
    
    // Gera palavra aleatória de 2-5 letras
    int tamanhoPalavra = 2 + (rand() % 4); // 2-5 letras
    if (tamanhoPalavra > numLetrasDisponiveis)
    {
        tamanhoPalavra = numLetrasDisponiveis;
    }
    
    // Copia letras disponíveis para poder remover as usadas
    TCHAR letrasTmp[MAXLETRAS];
    memcpy(letrasTmp, letrasDisponiveis, numLetrasDisponiveis * sizeof(TCHAR));
    int letrasTmpCount = numLetrasDisponiveis;
    
    // Constrói palavra aleatória
    for (int i = 0; i < tamanhoPalavra; i++)
    {
        if (letrasTmpCount == 0) break;
        
        int indiceAleatorio = rand() % letrasTmpCount;
        palavraAleatoria[i] = letrasTmp[indiceAleatorio];
        
        // Remove a letra usada (move a última para a posição da usada)
        letrasTmp[indiceAleatorio] = letrasTmp[letrasTmpCount - 1];
        letrasTmpCount--;
    }
    
    palavraAleatoria[tamanhoPalavra] = '\0';
    return palavraAleatoria;
}

// Função atualizada para encontrar palavra (com chance de palavra aleatória)
TCHAR* EncontrarMelhorPalavra(MemoriaPartilhada *mem)
{
    static TCHAR melhorPalavra[MAX_WORD_LENGTH];
    int melhorPontuacao = 0;
    melhorPalavra[0] = '\0';

    // 20% de chance de tentar uma palavra aleatória (que provavelmente não existe)
    if ((rand() % 100) < 20)
    {
        TCHAR *palavraAleatoria = GerarPalavraAleatoria(mem);
        if (palavraAleatoria)
        {
            _tcscpy(melhorPalavra, palavraAleatoria);
            //_tprintf(TEXT("[BOT] Tentar palavra aleatória: %s\n"), melhorPalavra);
            return melhorPalavra;
        }
    }

    // Procura palavras que podem ser formadas, priorizando as mais longas
    for (int i = 0; i < dictionarySize; i++)
    {
        if (PodeFormarPalavra(dictionary[i], mem))
        {
            int pontos = (int)_tcslen(dictionary[i]);
            if (pontos > melhorPontuacao)
            {
                melhorPontuacao = pontos;
                _tcscpy(melhorPalavra, dictionary[i]);
            }
        }
    }

    return melhorPalavra[0] != '\0' ? melhorPalavra : NULL;
}

// Função para conectar ao árbitro
BOOL ConectarArbitro()
{
    // Aguarda pipe
    if (!WaitNamedPipe(ARBITRO_PIPE_NAME, 5000))
    {
        _tprintf(TEXT("[BOT] Arbitro não responde\n"));
        return FALSE;
    }

    // Conecta
    g_hPipe = CreateFile(
        ARBITRO_PIPE_NAME,
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);

    if (g_hPipe == INVALID_HANDLE_VALUE)
    {
        _tprintf(TEXT("[BOT] Falha na conexão\n"));
        return FALSE;
    }

    // Envia mensagem de entrada
    Mensagem msg = {MSG_ENTRAR, {0}, {0}, 0};
    _tcscpy(msg.username, g_username);
    DWORD bytes;

    if (!WriteFile(g_hPipe, &msg, sizeof(msg), &bytes, NULL))
    {
        _tprintf(TEXT("[BOT] Falha ao enviar MSG_ENTRAR\n"));
        CloseHandle(g_hPipe);
        return FALSE;
    }

    // Recebe resposta
    Mensagem resp;
    if (!ReadFile(g_hPipe, &resp, sizeof(resp), &bytes, NULL))
    {
        _tprintf(TEXT("[BOT] Falha ao receber resposta\n"));
        CloseHandle(g_hPipe);
        return FALSE;
    }

    if (resp.tipo == MSG_SUCESSO)
    {
        _tprintf(TEXT("[BOT] %s conectado: %s\n"), g_username, resp.conteudo);
        return TRUE;
    }
    else
    {
        _tprintf(TEXT("[BOT] Conexão rejeitada: %s\n"), resp.conteudo);
        CloseHandle(g_hPipe);
        return FALSE;
    }
}

// Função para enviar palavra
BOOL EnviarPalavra(const TCHAR *palavra)
{
    Mensagem msg = {MSG_PALAVRA, {0}, {0}, 0};
    _tcscpy(msg.username, g_username);
    _tcscpy(msg.conteudo, palavra);
    DWORD bytes;

    if (!WriteFile(g_hPipe, &msg, sizeof(msg), &bytes, NULL))
    {
        return FALSE;
    }

    // Recebe resposta
    Mensagem resp;
    if (!ReadFile(g_hPipe, &resp, sizeof(resp), &bytes, NULL))
    {
        return FALSE;
    }

    // Verifica se o servidor está a encerrar
    if (resp.tipo == MSG_ERRO && _tcsstr(resp.conteudo, TEXT("Servidor a encerrar")) != NULL)
    {
        _tprintf(TEXT("[BOT] %s\n"), resp.conteudo);
        return FALSE;
    }

    g_pontuacao = resp.pontuacao;
    _tprintf(TEXT("[BOT] Tentou '%s': %s (Pontuacao: %.1f)\n"), 
             palavra, resp.conteudo, g_pontuacao);

    return TRUE;
}

// Função principal do bot
int _tmain(int argc, LPTSTR argv[])
{
#ifdef UNICODE
    _setmode(_fileno(stdout), _O_WTEXT);
#endif

    if (argc < 3)
    {
        _tprintf(TEXT("Uso: %s <username> <tempo_reacao_segundos>\n"), argv[0]);
        return 1;
    }

    // Lê parâmetros
    _tcsncpy(g_username, argv[1], MAX_USERNAME - 1);
    g_username[MAX_USERNAME - 1] = '\0';
    g_tempoReacao = _ttoi(argv[2]);

    _tprintf(TEXT("[BOT] Iniciando bot '%s' (tempo reação: %ds)\n"), 
             g_username, g_tempoReacao);

    // Carrega dicionário
    if (!LoadBotDictionary(TEXT("5000-more-common.txt")))
    {
        _tprintf(TEXT("[BOT] Continuando sem dicionário...\n"));
    }

    // Conecta ao árbitro
    if (!ConectarArbitro())
    {
        return 1;
    }

    // Mapeia memória partilhada para ler letras
    HANDLE hMap = OpenFileMapping(FILE_MAP_READ, FALSE, MEMORIA_PARTILHADA_NAME);
    if (!hMap)
    {
        _tprintf(TEXT("[BOT] Erro ao abrir memória partilhada\n"));
        CloseHandle(g_hPipe);
        return 1;
    }

    MemoriaPartilhada *mem = (MemoriaPartilhada *)MapViewOfFile(
        hMap, FILE_MAP_READ, 0, 0, sizeof(MemoriaPartilhada));
    if (!mem)
    {
        _tprintf(TEXT("[BOT] Erro ao mapear memoria partilhada\n"));
        CloseHandle(hMap);
        CloseHandle(g_hPipe);
        return 1;
    }

    // Loop principal do bot
    srand((unsigned)time(NULL));
    while (TRUE)
    {
        // Aguarda tempo de reação
        Sleep(g_tempoReacao * 1000);

        // Procura melhor palavra
        TCHAR *palavra = EncontrarMelhorPalavra(mem);
        if (palavra)
        {
            if (!EnviarPalavra(palavra))
            {
                break; // Servidor encerrou ou erro de comunicação
            }
        }
        else
        {
            _tprintf(TEXT("[BOT] Nenhuma palavra possivel encontrada\n"));
        }
    }

    // Cleanup
    UnmapViewOfFile(mem);
    CloseHandle(hMap);
    CloseHandle(g_hPipe);
    _tprintf(TEXT("[BOT] %s desconectado. Pontuação final: %.1f\n"), g_username, g_pontuacao);

    return 0;
}