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

int RITMO = 3;

// Declaração do critical section global para jogadores
CRITICAL_SECTION csJogadores;

MemoriaPartilhada* g_mem = NULL;

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
            _tprintf(TEXT("- %s: %d pontos\n"), g_mem->jogadores[i].username, g_mem->jogadores[i].pontuacao);
            count++;
        }
    }
    if (count == 0) {
        _tprintf(TEXT("Não há jogadores ativos no momento.\n"));
    }
    LeaveCriticalSection(&csJogadores);
}
BOOL excludePlayer(const TCHAR *username)
{
    extern MemoriaPartilhada *g_mem;
    BOOL result = RemoverJogador(g_mem, username);
    
    if (result) {
        _tprintf(TEXT("[ADMIN] excluir %s - jogador excluído com sucesso\n"), username);
    } else {
        _tprintf(TEXT("[ADMIN] excluir %s - jogador não encontrado\n"), username);
    }
    
    return result;
}
BOOL startBot(const TCHAR *botName)
{
    _tprintf(TEXT("[ADMIN] iniciarbot %s  - lançou bot automático\n"), botName);
    return TRUE;
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

        // insere em primeiro slot livre
        for (int i = 0; i < MAXLETRAS; ++i)
        {
            if (mem->estado[i] == 0)
            {
                mem->letras[i] = nova;
                mem->estado[i] = 1;
                goto next;
            }
        }
        // se não houver slot livre, recicla posição 0
        mem->letras[0] = nova;
mem->estado[0] = 1;
    next:;
    }
    return 0;
}

DWORD WINAPI ClientThread(LPVOID lpParam)
{
    CLIENT_PARAM *p = (CLIENT_PARAM *)lpParam;
    HANDLE hPipe = p->hPipe;
    MemoriaPartilhada *mem = p->mem;
    free(p);

    _tprintf(TEXT("[SISTEMA] Thread de cliente iniciada\n"));

    Mensagem msg, resp;
    DWORD bytes;
    int pontuacao = 0; // Pontuação do jogador

    // Ler a primeira mensagem do cliente e processar a entrada
    if (ReadFile(hPipe, &msg, sizeof(msg), &bytes, NULL)) {
        _tprintf(TEXT("[DEBUG] Recebido mensagem do tipo: %d de: %s\n"), msg.tipo, msg.username);
        
        // Processar a mensagem inicial (MSG_ENTRAR)
        ZeroMemory(&resp, sizeof(resp));
        _tcscpy(resp.username, msg.username);
        
        if (msg.tipo == MSG_ENTRAR) {
            resp.tipo = MSG_SUCESSO;
            
            _tprintf(TEXT("[DEBUG] Tentativa de conexão: %s\n"), msg.username);
            int slotJogador = AdicionarJogador(mem, msg.username);
            
            if (slotJogador >= 0) {
                _tcscpy(resp.conteudo, TEXT("Bem-vindo ao jogo!"));
                _tprintf(TEXT("[SISTEMA] Jogador %s conectado com sucesso (slot %d)\n"), 
                        msg.username, slotJogador);
            } else if (slotJogador == -1) {
                resp.tipo = MSG_ERRO;
                _tcscpy(resp.conteudo, TEXT("Nome de utilizador já existe!"));
                _tprintf(TEXT("[SISTEMA] Conexão rejeitada: Nome %s já existe\n"), msg.username);
            } else {
                resp.tipo = MSG_ERRO;
                _tcscpy(resp.conteudo, TEXT("Servidor cheio!"));
                _tprintf(TEXT("[SISTEMA] Conexão rejeitada: Servidor cheio\n"));
            }
            
            WriteFile(hPipe, &resp, sizeof(resp), &bytes, NULL);
        }
    } else {
        _tprintf(TEXT("[ERRO] Falha ao ler do pipe: %d\n"), GetLastError());
        goto cleanup;
    }

    while (ReadFile(hPipe, &msg, sizeof(msg), &bytes, NULL) && bytes == sizeof(msg))
    {
        ZeroMemory(&resp, sizeof(resp));
        _tcscpy(resp.username, msg.username);

        switch (msg.tipo)
        {
        case MSG_ENTRAR:
            resp.tipo = MSG_SUCESSO;
            
            _tprintf(TEXT("[DEBUG] Tentativa de conexão: %s\n"), msg.username);
            int slotJogador = AdicionarJogador(mem, msg.username);
            
            if (slotJogador >= 0) {
                _tcscpy(resp.conteudo, TEXT("Bem-vindo ao jogo!"));
                _tprintf(TEXT("[SISTEMA] Jogador %s conectado com sucesso (slot %d)\n"), 
                        msg.username, slotJogador);
            } else if (slotJogador == -1) {
                resp.tipo = MSG_ERRO;
                _tcscpy(resp.conteudo, TEXT("Nome de utilizador já existe!"));
                _tprintf(TEXT("[SISTEMA] Conexão rejeitada: Nome %s já existe\n"), msg.username);
            } else {
                resp.tipo = MSG_ERRO;
                _tcscpy(resp.conteudo, TEXT("Servidor cheio!"));
                _tprintf(TEXT("[SISTEMA] Conexão rejeitada: Servidor cheio\n"));
            }
            break;

        case MSG_PALAVRA:
            // Validar se a palavra pode ser formada com as letras disponíveis
            BOOL palavraValida = TRUE;
            TCHAR letrasTmp[MAXLETRAS];
            memcpy(letrasTmp, mem->letras, MAXLETRAS);

            for (int i = 0; msg.conteudo[i] != '\0'; i++)
            {
                BOOL encontrou = FALSE;
                for (int j = 0; j < MAXLETRAS; j++)
                {
                    if (_totupper(msg.conteudo[i]) == _totupper(letrasTmp[j]))
                    {
                        letrasTmp[j] = '_'; // marca como usada
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

            if (palavraValida)
            {
                pontuacao += _tcslen(msg.conteudo);
                resp.tipo = MSG_SUCESSO;
                _stprintf(resp.conteudo, TEXT("Palavra válida! +%d pontos"),
                          (int)_tcslen(msg.conteudo));
                resp.pontuacao = pontuacao;

                // Atualiza última palavra jogada
                _tcscpy(mem->ultima_palavra, msg.conteudo);

                // Atualiza pontuação na lista de jogadores
                EnterCriticalSection(&csJogadores);
                for (int i = 0; i < MAX_JOGADORES; i++)
                {
                    if (mem->jogadores[i].ativo && _tcscmp(mem->jogadores[i].username, msg.username) == 0)
                    {
                        mem->jogadores[i].pontuacao += _tcslen(msg.conteudo);
                        break;
                    }
                }
                LeaveCriticalSection(&csJogadores);
            }
            else
            {
                resp.tipo = MSG_ERRO;
                _tcscpy(resp.conteudo, TEXT("Palavra inválida!"));
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
            for (int i = 0; i < MAX_JOGADORES; i++) {
                if (mem->jogadores[i].ativo) {
                    TCHAR linha[64];
                    _stprintf(linha, TEXT("%s(%d pontos) "), mem->jogadores[i].username, mem->jogadores[i].pontuacao);
                    _tcscat(buffer, linha);
                    count++;
                }
            }
            LeaveCriticalSection(&csJogadores);
            if (count == 0) {
                _tcscpy(resp.conteudo, TEXT("Nenhum jogador ativo."));
            } else {
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
                    break;
                }
            }
            LeaveCriticalSection(&csJogadores);
            
            // Verificar se só resta 1 jogador
            if (contarJogadoresAtivos() <= 1)
            {
                _tprintf(TEXT("[SISTEMA] Apenas um jogador restante. Encerrando jogo...\n"));
                signalShutdown();
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

int _tmain(int argc, LPTSTR argv[])
{

    // Inicializa o critical section global
    InitializeCriticalSection(&csJogadores);

    // Configura consola Unicode
#ifdef UNICODE
    _setmode(_fileno(stdin), _O_WTEXT);
    _setmode(_fileno(stdout), _O_WTEXT);
#endif


    _ftprintf(stderr, TEXT("ADMIN ON.\n"));

    hShutdownEvent = CreateEvent(NULL, TRUE, FALSE, TEXT("Global\\SO2_ShutdownEvent"));
    if (!hShutdownEvent)
    {
        _tprintf(TEXT("[ERRO] não conseguiu criar o evento de shutdown\n"));
        return 1;
    }

    if (!InitializeRegistry())
    {
        _tprintf(TEXT("[ERRO] Falha ao inicializar Registry\n"));
        return 1;
    }

    // --- 2) Lança a thread de adm (Com esta thread é possivel escrever comandos na consola)
    DWORD adminTid;
    HANDLE hAdminThread = CreateThread(
        NULL,        // Segurança padrão
        0,           // Tamanho da stack
        AdminThread, // Função
        NULL,        // Parâmetro
        0,           // Flags
        &adminTid);

    if (!hAdminThread)
    {
        _tprintf(TEXT("[ERRO] não conseguiu criar AdminThread\n"));
        return 1;
    }

    // Garante instância única do árbitro
    HANDLE hMutex = CreateMutex(NULL, TRUE, ARBITRO_MUTEX_NAME);
    if (!hMutex || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        _tprintf(TEXT("[ERRO] Já existe uma instância do árbitro.\n"));
        return 1;
    }

    // Criar e inicializar memória partilhada
    HANDLE hMap = CreateFileMapping(
        INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
        0, sizeof(MemoriaPartilhada), MEMORIA_PARTILHADA_NAME);
    if (!hMap)
    {
        _tprintf(TEXT("[ERRO] CreateFileMapping falhou.\n"));
        return 1;
    }

    MemoriaPartilhada *mem = (MemoriaPartilhada *)MapViewOfFile(
        hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(MemoriaPartilhada));
    if (!mem)
    {
        _tprintf(TEXT("[ERRO] MapViewOfFile falhou.\n"));
        CloseHandle(hMap);
        return 1;
    }

    // assign the global pointer
    g_mem = mem;

    mem->numJogadores = 0;
    for (int i = 0; i < MAX_JOGADORES; i++)
    {
        mem->jogadores[i].ativo = FALSE;
        mem->jogadores[i].pontuacao = 0;
        mem->jogadores[i].username[0] = TEXT('\0');
    }

    // Inicializa vetor com as letras
    for (int i = 0; i < MAXLETRAS; i++)
    {
        mem->letras[i] = TEXT('_');
        mem->estado[i] = 0;
    }
    mem->ultima_palavra[0] = TEXT('\0');

    // **2)** Lança a Thread que fica a gerar letras a cada    !!!!!!!! RITMO segundos
    srand((unsigned)time(NULL));

    // pré-enche as MAXLETRAS primeiras posições
    for (int i = 0; i < MAXLETRAS; ++i)
    {
#ifdef UNICODE
        mem->letras[i] = (TCHAR)(L'A' + (rand() % 26));
#else
        mem->letras[i] = 'A' + (rand() % 26);
#endif
        mem->estado[i] = 1;
    }

    HANDLE hLetterThread = CreateThread(
        NULL, 0,
        LetterThread,
        mem, // passa o ponteiro para a memória partilhada
        0, NULL);
    if (!hLetterThread)
    {
        _tprintf(TEXT("[ERRO] não conseguiu criar LetterThread\n"));
    }

    // Loop principal: cria uma instância por cliente
    while (WaitForSingleObject(hShutdownEvent, 0) != WAIT_OBJECT_0)
    {
        // 1) Cria instância do pipe
        HANDLE hPipe = CreateNamedPipe(
            ARBITRO_PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            MAX_JOGADORES, sizeof(Mensagem), sizeof(Mensagem),
            0, NULL);
        if (hPipe == INVALID_HANDLE_VALUE)
            break;

        // 2) Aguarda ligação
        BOOL ok = ConnectNamedPipe(hPipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED;
        if (!ok)
        {
            CloseHandle(hPipe);
            continue;
        }
        
        _tprintf(TEXT("[SISTEMA] Nova conexão de cliente recebida!\n"));

        // 3) Despacha thread para tratar o cliente
        CLIENT_PARAM *cp = malloc(sizeof(*cp));
        cp->hPipe = hPipe;
        cp->mem = mem;
        HANDLE hThread = CreateThread(NULL, 0, ClientThread, cp, 0, NULL);
        if (hThread)
            CloseHandle(hThread);
        else
        {
            // Limpeza final
            UnmapViewOfFile(mem);
            CloseHandle(hMap);
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            CloseHandle(hAdminThread);
            CloseHandle(hLetterThread);
            CloseHandle(hShutdownEvent);
            DeleteCriticalSection(&csJogadores);

            // Apagar a memória partilhada
            DeleteCriticalSection(&csJogadores);

            return 0;
        }
        
    }

    // Limpeza final
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
