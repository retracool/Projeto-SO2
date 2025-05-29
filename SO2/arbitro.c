#pragma comment(lib, "Advapi32.lib")
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
#include <fcntl.h>
#include <time.h>
#include <io.h>
#include "estrutura.h"

#define MAX_WORDS 5005
#define MAX_WORD_LENGTH 50

// Dicionário global
static TCHAR dictionary[MAX_WORDS][MAX_WORD_LENGTH];
static int dictionarySize = 0;

BOOL LoadDictionary(const TCHAR *filename);
BOOL IsWordInDictionary(const TCHAR *word);
int RITMO = 3;

// Declaração do critical section global para jogadores
CRITICAL_SECTION csJogadores;

MemoriaPartilhada *g_mem = NULL;

BOOL InitializeRegistry()
{
    HKEY chave;
    DWORD disposition;
    if (RegCreateKeyEx(HKEY_CURRENT_USER,
                       TEXT("Software\\SO2_Jogo"),
                       0, NULL, REG_OPTION_NON_VOLATILE,
                       KEY_ALL_ACCESS, NULL, &chave, &disposition) == ERROR_SUCCESS)
    {
        DWORD maxLetrasValue = MAXLETRAS;
        // Se é primeira execução, define valores default
        if (disposition == REG_CREATED_NEW_KEY)
        {
            RegSetValueEx(chave, TEXT("RITMO"), 0, REG_DWORD, (BYTE *)&RITMO, sizeof(DWORD));

            RegSetValueEx(chave, TEXT("MAXLETRAS"), 0, REG_DWORD, (BYTE *)&maxLetrasValue, sizeof(DWORD));
        }
        else
        {
            // Lê valores existentes
            DWORD tamanho = sizeof(DWORD);
            RegQueryValueEx(chave, TEXT("RITMO"), NULL, NULL, (BYTE *)&RITMO, &tamanho);
            RegQueryValueEx(chave, TEXT("MAXLETRAS"), NULL, NULL, (BYTE *)&maxLetrasValue, &tamanho);
        }
        RegCloseKey(chave);
        return TRUE;
    }
    return FALSE;
}

// --- Evento para sinalizar encerramento ---
static HANDLE hShutdownEvent = NULL;

// --- Stubs das Funções de Admin ---
void listPlayers(void)
{
    extern MemoriaPartilhada *g_mem;

    EnterCriticalSection(&csJogadores);
    _tprintf(TEXT("Lista de jogadores (Total ativos: %d):\n"), g_mem->numJogadores);
    int count = 0;
    for (int i = 0; i < MAX_JOGADORES; i++)
    {
        if (g_mem->jogadores[i].ativo)
        {
            _tprintf(TEXT("- %s: %.1f pontos\n"), g_mem->jogadores[i].username, g_mem->jogadores[i].pontuacao);
            count++;
        }
    }
    if (count == 0)
    {
        _tprintf(TEXT("Não há jogadores ativos no momento.\n"));
    }
    LeaveCriticalSection(&csJogadores);
}
BOOL excludePlayer(const TCHAR *username)
{
    extern MemoriaPartilhada *g_mem;
    BOOL result = RemoverJogador(g_mem, username);

    if (result)
    {
        _tprintf(TEXT("[ADMIN] excluir %s - jogador excluído com sucesso\n"), username);
    }
    else
    {
        _tprintf(TEXT("[ADMIN] excluir %s - jogador não encontrado\n"), username);
    }

    return result;
}
BOOL startBot(const TCHAR *botName)
{
    // Verifica se o nome já existe
    EnterCriticalSection(&csJogadores);
    for (int i = 0; i < MAX_JOGADORES; i++)
    {
        if (g_mem->jogadores[i].ativo && _tcscmp(g_mem->jogadores[i].username, botName) == 0)
        {
            LeaveCriticalSection(&csJogadores);
            _tprintf(TEXT("[ADMIN] iniciarbot %s - ERRO: Username já existe\n"), botName);
            return FALSE;
        }
    }
    LeaveCriticalSection(&csJogadores);

    // Gera tempo de reação aleatório entre 5 e 30 segundos
    int tempoReacao = 5 + (rand() % 26); // 5-30 segundos

    // Prepara linha de comando para o bot
    TCHAR cmdLine[256];
    _stprintf(cmdLine, TEXT("bot.exe \"%s\" %d"), botName, tempoReacao);

    // Lança o processo bot
    STARTUPINFO si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);

    if (CreateProcess(
        TEXT("bot.exe"),     // nome do executável
        cmdLine,             // linha de comando
        NULL,                // security attributes do processo
        NULL,                // security attributes da thread
        FALSE,               // herdar handles
        0,                   // flags de criação
        NULL,                // environment
        NULL,                // diretório atual
        &si,                 // startup info
        &pi))                // process info
    {
        // Fecha handles - não precisamos de monitorizar o processo
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        
        _tprintf(TEXT("[ADMIN] iniciarbot %s - Bot lançado (tempo de reacao: %ds)\n"), 
                 botName, tempoReacao);
        return TRUE;
    }
    else
    {
        _tprintf(TEXT("[ADMIN] iniciarbot %s - ERRO: Falha ao iniciar bot (%d)\n"), 
                 botName, GetLastError());
        return FALSE;
    }
}
void changeCadencia(int delta)
{

    RITMO += delta;

    // Garantir que RITMO não fique negativo ou nulo
    if (RITMO < 1)
        RITMO = 1;

    if (delta < 0)
        _tprintf(TEXT("[ADMIN] acelerar  - diminuiu intervalo entre letras para %d\n"), RITMO);
    else
        _tprintf(TEXT("[ADMIN] travar    - aumentou intervalo entre letras para %d\n"), RITMO);

    // Update registry with new value
    HKEY chave;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, TEXT("Software\\SO2_Jogo"), 0, KEY_WRITE, &chave) == ERROR_SUCCESS)
    {
        RegSetValueEx(chave, TEXT("RITMO"), 0, REG_DWORD, (BYTE *)&RITMO, sizeof(DWORD));
        RegCloseKey(chave);
    }
}
void signalShutdown(void)
{
    _tprintf(TEXT("[ADMIN] encerrar  - sinal de encerramento enviado\n"));
    // dispara o evento de shutdown

    if (hShutdownEvent)
        SetEvent(hShutdownEvent);
}

// --- Thread que processa comandos do administrador ---
DWORD WINAPI AdminThread(LPVOID lpParam)
{
    TCHAR linha[128];
    while (_fgetts(linha, (sizeof(linha) / sizeof(linha[0])), stdin))
    {
        size_t len = _tcslen(linha);
        if (len > 0 && linha[len - 1] == TEXT('\n'))
            linha[len - 1] = TEXT('\0');

        if (_tcscmp(linha, TEXT("listar")) == 0)
        {
            listPlayers();
        }
        else if (_tcsncmp(linha, TEXT("excluir "), 8) == 0)
        {
            const TCHAR *user = linha + 8;
            excludePlayer(user);
        }
        else if (_tcsncmp(linha, TEXT("iniciarbot "), 11) == 0)
        {
            const TCHAR *botName = linha + 11;
            startBot(botName);
        }
        else if (_tcscmp(linha, TEXT("acelerar")) == 0)
        {
            changeCadencia(-1);
        }
        else if (_tcscmp(linha, TEXT("travar")) == 0)
        {
            changeCadencia(+1);
        }
        else if (_tcscmp(linha, TEXT("encerrar")) == 0)
        {
            signalShutdown();
            break;
        }
        else
        {
            _tprintf(TEXT("[ADMIN] Comando desconhecido: '%s'\n"), linha);
        }
    }
    return 0;
}

// Thread que gera letras a cada RITMO segundos
DWORD WINAPI LetterThread(LPVOID p)
{
    MemoriaPartilhada *mem = (MemoriaPartilhada *)p;
    
    while (WaitForSingleObject(hShutdownEvent, RITMO * 1000) == WAIT_TIMEOUT)
    {
#ifdef UNICODE
        TCHAR nova = (TCHAR)(L'A' + (rand() % 26));
#else
        char nova = 'A' + (rand() % 26);
#endif

        DWORD agora = GetTickCount();

        // Procura primeira posição livre (estado = 0)
        int posicaoLivre = -1;
        for (int i = 0; i < MAXLETRAS; ++i)
        {
            if (mem->estado[i] == 0)
            {
                posicaoLivre = i;
                break;
            }
        }

        if (posicaoLivre != -1)
        {
            // Há espaço livre - usa essa posição
            mem->letras[posicaoLivre] = nova;
            mem->estado[posicaoLivre] = 1;
            mem->timestamp[posicaoLivre] = agora;
        }
        else
        {
            // Não há espaço livre - encontra a letra mais antiga
            DWORD timestampMaisAntigo = agora;
            int posicaoMaisAntiga = 0;
            
            for (int i = 0; i < MAXLETRAS; i++)
            {
                if (mem->estado[i] == 1 && mem->timestamp[i] < timestampMaisAntigo)
                {
                    timestampMaisAntigo = mem->timestamp[i];
                    posicaoMaisAntiga = i;
                }
            }
            
            // Substitui a letra mais antiga
            mem->letras[posicaoMaisAntiga] = nova;
            mem->estado[posicaoMaisAntiga] = 1;
            mem->timestamp[posicaoMaisAntiga] = agora;
            
            //_tprintf(TEXT("[DEBUG] Letra '%c' substituiu letra '%c' (posição %d)\n"), 
            //         nova, mem->letras[posicaoMaisAntiga], posicaoMaisAntiga);
        }
    }
    return 0;
}

DWORD WINAPI ClientThread(LPVOID lpParam)
{
    CLIENT_PARAM *p = (CLIENT_PARAM *)lpParam;
    HANDLE hPipe = p->hPipe;
    MemoriaPartilhada *mem = p->mem;
    free(p);

    // VERIFICAÇÃO DE SHUTDOWN NO INÍCIO
    if (WaitForSingleObject(hShutdownEvent, 0) == WAIT_OBJECT_0) {
        Mensagem resp = { MSG_ERRO, {0}, TEXT("Servidor a encerrar..."), 0 };
        DWORD bytes;
        WriteFile(hPipe, &resp, sizeof(resp), &bytes, NULL);
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
        return 0;
    }

    _tprintf(TEXT("[SISTEMA] Thread de cliente iniciada\n"));

    Mensagem msg, resp;
    DWORD bytes;
    float pontuacao = 0;

    while (ReadFile(hPipe, &msg, sizeof(msg), &bytes, NULL) && bytes == sizeof(msg))
    {
        // VERIFICA SHUTDOWN A CADA CICLO
        if (WaitForSingleObject(hShutdownEvent, 0) == WAIT_OBJECT_0) {
            Mensagem shutdownResp = { MSG_ERRO, {0}, TEXT("Servidor a encerrar..."), 0 };
            _tcscpy(shutdownResp.username, msg.username);
            WriteFile(hPipe, &shutdownResp, sizeof(shutdownResp), &bytes, NULL);
            goto cleanup;
        }

        ZeroMemory(&resp, sizeof(resp));
        _tcscpy(resp.username, msg.username);

        switch (msg.tipo)
        {
        case MSG_ENTRAR:
            resp.tipo = MSG_SUCESSO;

            //_tprintf(TEXT("[DEBUG] Tentativa de conexão: %s\n"), msg.username);
            int slotJogador = AdicionarJogador(mem, msg.username);

            if (slotJogador >= 0)
            {
                _tcscpy(resp.conteudo, TEXT("Bem-vindo ao jogo!"));
                _tprintf(TEXT("[SISTEMA] Jogador %s conectado com sucesso (slot %d)\n"),
                         msg.username, slotJogador);
            }
            else if (slotJogador == -1)
            {
                resp.tipo = MSG_ERRO;
                _tcscpy(resp.conteudo, TEXT("Nome de utilizador já existe!"));
                _tprintf(TEXT("[SISTEMA] Conexão rejeitada: Nome %s já existe\n"), msg.username);
            }
            else
            {
                resp.tipo = MSG_ERRO;
                _tcscpy(resp.conteudo, TEXT("Servidor cheio!"));
                _tprintf(TEXT("[SISTEMA] Conexão rejeitada: Servidor cheio\n"));
            }
            break;

        case MSG_PALAVRA:
            // Validar se a palavra pode ser formada com as letras disponíveis
            BOOL palavraValida = TRUE;
            TCHAR letrasTmp[MAXLETRAS];
            int posicoesSelecionadas[MAXLETRAS]; // Para guardar as posições das letras usadas
            int numLetrasUsadas = 0;
            
            memcpy(letrasTmp, mem->letras, MAXLETRAS * sizeof(TCHAR));

            for (int i = 0; msg.conteudo[i] != '\0'; i++)
            {
                BOOL encontrou = FALSE;
                TCHAR letraProcurada = _totupper(msg.conteudo[i]);

                for (int j = 0; j < MAXLETRAS; j++)
                {
                    if (mem->estado[j] == 1 && letraProcurada == _totupper(letrasTmp[j]))
                    {
                        letrasTmp[j] = '_'; // marca temporariamente como usada
                        posicoesSelecionadas[numLetrasUsadas] = j; // guarda a posição
                        numLetrasUsadas++;
                        encontrou = TRUE;
                        break;
                    }
                }
                if (!encontrou)
                {
                    palavraValida = FALSE;
                    break;
                }
            }

            int tamPalavra = (int)_tcslen(msg.conteudo);

            // Verifica se a palavra está no dicionário (se carregado)
            if (palavraValida && dictionarySize > 0)
            {
                if (!IsWordInDictionary(msg.conteudo))
                {
                    palavraValida = FALSE;
                    resp.tipo = MSG_ERRO;
                    _tcscpy(resp.conteudo, TEXT("Palavra não encontrada no dicionário!"));
                }
            }

            if (palavraValida)
            {
                // Remove as letras usadas da memória partilhada
                for (int i = 0; i < numLetrasUsadas; i++)
                {
                    int pos = posicoesSelecionadas[i];
                    mem->estado[pos] = 0;
                    mem->letras[pos] = '_';
                    mem->timestamp[pos] = 0;
                }

                pontuacao += tamPalavra;
                resp.tipo = MSG_SUCESSO;
                _stprintf(resp.conteudo, TEXT("Palavra válida! +%.1f pontos"), (float)tamPalavra); // CORRIGE AQUI
                resp.pontuacao = pontuacao;

                // Atualiza última palavra jogada
                _tcscpy(mem->ultima_palavra, msg.conteudo);

                // Atualiza pontuação na lista de jogadores
                EnterCriticalSection(&csJogadores);
                for (int i = 0; i < MAX_JOGADORES; i++)
                {
                    if (mem->jogadores[i].ativo && _tcscmp(mem->jogadores[i].username, msg.username) == 0)
                    {
                        mem->jogadores[i].pontuacao += tamPalavra;
                        break;
                    }
                }
                LeaveCriticalSection(&csJogadores);

                _tprintf(TEXT("[SISTEMA] Jogador %s usou %d letras. Letras removidas.\n"), 
                         msg.username, numLetrasUsadas);
            }
            else
            {
                // Palavra errada: desconta meio ponto por letra
                float desconto = tamPalavra * 0.5f;
                pontuacao -= desconto;
                resp.tipo = MSG_ERRO;
                _stprintf(resp.conteudo, TEXT("Palavra inválida! -%.1f pontos"), desconto); // Já estava correto
                resp.pontuacao = pontuacao;

                // Atualiza pontuação na lista de jogadores
                EnterCriticalSection(&csJogadores);
                for (int i = 0; i < MAX_JOGADORES; i++)
                {
                    if (mem->jogadores[i].ativo && _tcscmp(mem->jogadores[i].username, msg.username) == 0)
                    {
                        mem->jogadores[i].pontuacao -= desconto;
                        break;
                    }
                }
                LeaveCriticalSection(&csJogadores);
            }
            break;

        case MSG_PONTUACAO:
            resp.tipo = MSG_PONTUACAO;
            _stprintf(resp.conteudo, TEXT("Pontuação atual"));
            resp.pontuacao = pontuacao;
            break;

        case MSG_JOGADORES:
            resp.tipo = MSG_SUCESSO;
            EnterCriticalSection(&csJogadores);
            TCHAR buffer[256] = TEXT("");
            int count = 0;
            for (int i = 0; i < MAX_JOGADORES; i++)
            {
                if (mem->jogadores[i].ativo)
                {
                    TCHAR linha[64];
                    _stprintf(linha, TEXT("%s(%.1f pontos) "), mem->jogadores[i].username, mem->jogadores[i].pontuacao);
                    _tcscat(buffer, linha);
                    count++;
                }
            }
            LeaveCriticalSection(&csJogadores);
            if (count == 0)
            {
                _tcscpy(resp.conteudo, TEXT("Nenhum jogador ativo."));
            }
            else
            {
                _tcscpy(resp.conteudo, buffer);
            }
            break;

        case MSG_SAIR:
            resp.tipo = MSG_SUCESSO;
            _tcscpy(resp.conteudo, TEXT("Até à próxima!"));
            WriteFile(hPipe, &resp, sizeof(resp), &bytes, NULL);

            // Marcar jogador como inativo
            EnterCriticalSection(&csJogadores);
            for (int i = 0; i < MAX_JOGADORES; i++)
            {
                if (mem->jogadores[i].ativo && _tcscmp(mem->jogadores[i].username, msg.username) == 0)
                {
                    mem->jogadores[i].ativo = FALSE;
                    mem->numJogadores--; // ADICIONA ESTA LINHA
                    break;
                }
            }
            
            // Verificar se só resta 1 jogador ou menos
            int jogadoresAtivos = 0;
            for (int i = 0; i < MAX_JOGADORES; i++) {
                if (mem->jogadores[i].ativo) jogadoresAtivos++;
            }
            LeaveCriticalSection(&csJogadores);
            
            if (jogadoresAtivos <= 1) {
                _tprintf(TEXT("[SISTEMA] Apenas %d jogador(es) restante(s). Encerrando jogo...\n"), jogadoresAtivos);
                
                // Sinaliza encerramento para todas as threads
                signalShutdown();
                
                // Pequena pausa para permitir que as threads processem o shutdown
                Sleep(500);
            }
            goto cleanup;

        default:
            resp.tipo = MSG_ERRO;
            _tcscpy(resp.conteudo, TEXT("Comando desconhecido"));
            break;
        }

        WriteFile(hPipe, &resp, sizeof(resp), &bytes, NULL);
    }

cleanup:
    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);
    return 0;
}

// Adiciona novo jogador
int AdicionarJogador(MemoriaPartilhada *mem, const TCHAR *username)
{
    int result = -2; // Default: no slots
    EnterCriticalSection(&csJogadores);

    // Verifica se jogador já existe
    for (int i = 0; i < MAX_JOGADORES; i++)
    {
        if (mem->jogadores[i].ativo && _tcscmp(mem->jogadores[i].username, username) == 0)
        {
            result = -1; // jogador já existe
            goto cleanup;
        }
    }

    // Procura slot livre
    for (int i = 0; i < MAX_JOGADORES; i++)
    {
        if (!mem->jogadores[i].ativo)
        {
            _tcscpy(mem->jogadores[i].username, username);
            mem->jogadores[i].pontuacao = 0;
            mem->jogadores[i].ativo = TRUE;
            mem->jogadores[i].lastActivity = GetTickCount64();
            mem->numJogadores++;
            result = i;
            goto cleanup;
        }
    }

cleanup:
    LeaveCriticalSection(&csJogadores);
    return result;
}

// Remove jogador
BOOL RemoverJogador(MemoriaPartilhada *mem, const TCHAR *username)
{
    EnterCriticalSection(&csJogadores);

    for (int i = 0; i < MAX_JOGADORES; i++)
    {
        if (mem->jogadores[i].ativo && _tcscmp(mem->jogadores[i].username, username) == 0)
        {
            mem->jogadores[i].ativo = FALSE;
            mem->numJogadores--;
            LeaveCriticalSection(&csJogadores);
            return TRUE;
        }
    }

    LeaveCriticalSection(&csJogadores);
    return FALSE;
}

// Atualiza pontuação do jogador
BOOL AtualizarPontuacao(MemoriaPartilhada *mem, const TCHAR *username, int novaPontuacao)
{
    EnterCriticalSection(&csJogadores);

    for (int i = 0; i < MAX_JOGADORES; i++)
    {
        if (mem->jogadores[i].ativo && _tcscmp(mem->jogadores[i].username, username) == 0)
        {
            mem->jogadores[i].pontuacao = novaPontuacao;
            mem->jogadores[i].lastActivity = GetTickCount64();
            LeaveCriticalSection(&csJogadores);
            return TRUE;
        }
    }

    LeaveCriticalSection(&csJogadores);
    return FALSE;
}

// Função para contar jogadores ativos
int contarJogadoresAtivos()
{
    int contador = 0;
    EnterCriticalSection(&csJogadores);
    for (int i = 0; i < MAX_JOGADORES; i++)
    {
        if (g_mem->jogadores[i].ativo)
            contador++;
    }
    LeaveCriticalSection(&csJogadores);
    return contador;
}

BOOL LoadDictionary(const TCHAR *filename)
{
    FILE *file;
#ifdef UNICODE
    file = _wfopen(filename, L"r, ccs=UTF-8");
#else
    file = fopen(filename, "r");
#endif

    if (!file)
    {
        _tprintf(TEXT("[ERRO] Não foi possível abrir o dicionário: %s\n"), filename);
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
    _tprintf(TEXT("[SISTEMA] Dicionário carregado: %d palavras\n"), dictionarySize);
    return TRUE;
}

BOOL IsWordInDictionary(const TCHAR *word)
{
    TCHAR upperWord[MAX_WORD_LENGTH];
    _tcsncpy(upperWord, word, MAX_WORD_LENGTH - 1);
    upperWord[MAX_WORD_LENGTH - 1] = '\0';

    // Converte para maiúsculas
    for (int i = 0; upperWord[i]; i++)
    {
        upperWord[i] = _totupper(upperWord[i]);
    }

    //_tprintf(TEXT("[DEBUG] Procurando '%s' no dicionário...\n"), upperWord);

    // Pesquisa linear
    for (int i = 0; i < dictionarySize; i++)
    {
        if (_tcscmp(dictionary[i], upperWord) == 0)
        {
            //_tprintf(TEXT("[DEBUG] Palavra encontrada no índice %d\n"), i);
            return TRUE;
        }
    }
    //_tprintf(TEXT("[DEBUG] Palavra não encontrada\n"));
    return FALSE;
}

// MAIN

int _tmain(int argc, LPTSTR argv[])
{
    InitializeCriticalSection(&csJogadores);

#ifdef UNICODE
    _setmode(_fileno(stdin), _O_WTEXT);
    _setmode(_fileno(stdout), _O_WTEXT);
#endif

    _ftprintf(stderr, TEXT("ADMIN ON.\n"));

    hShutdownEvent = CreateEvent(NULL, TRUE, FALSE, TEXT("Global\\SO2_ShutdownEvent"));
    if (!hShutdownEvent)
        return 1;

    if (!InitializeRegistry())
        return 1;

    if (!LoadDictionary(TEXT("5000-more-common.txt")))
        _tprintf(TEXT("[AVISO] Continuando sem dicionário...\n"));

    DWORD adminTid;
    HANDLE hAdminThread = CreateThread(
        NULL, 0, AdminThread, NULL, 0, &adminTid);

    if (!hAdminThread)
        return 1;

    HANDLE hMutex = CreateMutex(NULL, TRUE, ARBITRO_MUTEX_NAME);
    if (!hMutex || GetLastError() == ERROR_ALREADY_EXISTS)
        return 1;

    HANDLE hMap = CreateFileMapping(
        INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
        0, sizeof(MemoriaPartilhada), MEMORIA_PARTILHADA_NAME);
    if (!hMap)
        return 1;

    MemoriaPartilhada *mem = (MemoriaPartilhada *)MapViewOfFile(
        hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(MemoriaPartilhada));
    if (!mem) {
        CloseHandle(hMap);
        return 1;
    }

    g_mem = mem;

    mem->numJogadores = 0;
    for (int i = 0; i < MAX_JOGADORES; i++) {
        mem->jogadores[i].ativo = FALSE;
        mem->jogadores[i].pontuacao = 0;
        mem->jogadores[i].username[0] = TEXT('\0');
    }

    // Inicializa todas as posições como vazias
    for (int i = 0; i < MAXLETRAS; i++) {
        mem->letras[i] = TEXT('_');
        mem->estado[i] = 0;
        mem->timestamp[i] = 0;
    }
    mem->ultima_palavra[0] = TEXT('\0');

    srand((unsigned)time(NULL));
    
    // Pré-enche apenas algumas posições iniciais (não todas)
    int letrasIniciais = MAXLETRAS / 2; // Começa com metade das letras
    DWORD timestampInicial = GetTickCount();
    for (int i = 0; i < letrasIniciais; ++i) {
#ifdef UNICODE
        mem->letras[i] = (TCHAR)(L'A' + (rand() % 26));
#else
        mem->letras[i] = 'A' + (rand() % 26);
#endif
        mem->estado[i] = 1;
        mem->timestamp[i] = timestampInicial + (i * 100);
    }

    HANDLE hLetterThread = CreateThread(
        NULL, 0, LetterThread, mem, 0, NULL);
    if (!hLetterThread)
        _tprintf(TEXT("[ERRO] não conseguiu criar LetterThread\n"));

    while (WaitForSingleObject(hShutdownEvent, 0) != WAIT_OBJECT_0)
    {
        HANDLE hPipe = CreateNamedPipe(
            ARBITRO_PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            MAX_JOGADORES, sizeof(Mensagem), sizeof(Mensagem),
            0, NULL);
        if (hPipe == INVALID_HANDLE_VALUE)
            break;

        // ADICIONA VERIFICAÇÃO AQUI
        if (WaitForSingleObject(hShutdownEvent, 0) == WAIT_OBJECT_0) {
            CloseHandle(hPipe);
            break;
        }

        BOOL ok = ConnectNamedPipe(hPipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED;
        if (!ok) {
            CloseHandle(hPipe);
            continue;
        }

        // VERIFICAÇÃO ADICIONAL ANTES DE CRIAR THREAD
        if (WaitForSingleObject(hShutdownEvent, 0) == WAIT_OBJECT_0) {
            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
            break;
        }

        _tprintf(TEXT("[SISTEMA] Nova conexão de cliente recebida!\n"));

        CLIENT_PARAM *cp = malloc(sizeof(*cp));
        cp->hPipe = hPipe;
        cp->mem = mem;
        HANDLE hThread = CreateThread(NULL, 0, ClientThread, cp, 0, NULL);
        if (hThread)
            CloseHandle(hThread);
        else
            break;
    }

    _tprintf(TEXT("[SISTEMA] Servidor a encerrar...\n"));

    UnmapViewOfFile(mem);
    CloseHandle(hMap);
    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
    CloseHandle(hAdminThread);
    CloseHandle(hLetterThread);
    CloseHandle(hShutdownEvent);
    DeleteCriticalSection(&csJogadores);

    return 0;
}
