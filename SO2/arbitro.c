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

// --- Evento para sinalizar encerramento ---
static HANDLE hShutdownEvent = NULL;

// --- Stubs das Funções de Admin ---
void listPlayers(void) {
    _tprintf(TEXT("[ADMIN] listar  - listou jogadores e pontuações\n"));
}
BOOL excludePlayer(const TCHAR* username) {
    _tprintf(TEXT("[ADMIN] excluir %s  - excluiu jogador\n"), username);
    return TRUE;
}
BOOL startBot(const TCHAR* botName) {
    _tprintf(TEXT("[ADMIN] iniciarbot %s  - lançou bot automático\n"), botName);
    return TRUE;
}
void changeCadencia(int delta) {
    if (delta < 0)
        _tprintf(TEXT("[ADMIN] acelerar  - diminuiu intervalo entre letras\n"));
    else
        _tprintf(TEXT("[ADMIN] travar    - aumentou intervalo entre letras\n"));
}
void signalShutdown(void) {
    _tprintf(TEXT("[ADMIN] encerrar  - sinal de encerramento enviado\n"));
    // dispara o evento de shutdown
    
    if (hShutdownEvent) SetEvent(hShutdownEvent);
}



// --- Thread que processa comandos do administrador ---
DWORD WINAPI AdminThread(LPVOID lpParam)
{
    TCHAR linha[128];
    while (_fgetts(linha, (sizeof(linha) / sizeof(linha[0]))
        , stdin)) {
        size_t len = _tcslen(linha);
        if (len > 0 && linha[len - 1] == TEXT('\n'))
            linha[len - 1] = TEXT('\0');

        if (_tcscmp(linha, TEXT("listar")) == 0) {
            listPlayers();
        }
        else if (_tcsncmp(linha, TEXT("excluir "), 8) == 0) {
            const TCHAR* user = linha + 8;
            excludePlayer(user);
        }
        else if (_tcscmp(linha, TEXT("iniciarbot ")) == 0) {
            const TCHAR* botName = linha + 11;
            startBot(botName);
        }
        else if (_tcscmp(linha, TEXT("acelerar")) == 0) {
            changeCadencia(-1);
        }
        else if (_tcscmp(linha, TEXT("travar")) == 0) {
            changeCadencia(+1);
        }
        else if (_tcscmp(linha, TEXT("encerrar")) == 0) {
            signalShutdown();
            break;
        }
        else {
            _tprintf(TEXT("[ADMIN] Comando desconhecido: '%s'\n"), linha);
        }
    }
    return 0; 
}

// vais ler isto do registry ou usar default
int RITMO = 3;  // segundos entre letras

// Thread que gera letras a cada RITMO segundos
DWORD WINAPI LetterThread(LPVOID p) {
    MemoriaPartilhada* mem = (MemoriaPartilhada*)p;
    while (WaitForSingleObject(hShutdownEvent, RITMO * 1000) == WAIT_TIMEOUT) {
        char nova = 'A' + (rand() % 26);

        // insere em primeiro slot livre
        for (int i = 0; i < MAXLETRAS; ++i) {
            if (mem->estado[i] == 0) {
                mem->letras[i] = nova;
                mem->estado[i] = 1;
                goto next;
            }
        }
        // se não houver slot livre, recicla posição 0
        mem->letras[0] = nova;
    next:;
    }
    return 0;
}


DWORD WINAPI ClientThread(LPVOID lpParam) {
    CLIENT_PARAM* p = (CLIENT_PARAM*)lpParam;
    HANDLE hPipe = p->hPipe;
    free(p);

    Mensagem msg;
    DWORD bytes;
    // Loop de leitura/escrita deste cliente
    while (ReadFile(hPipe, &msg, sizeof(msg), &bytes, NULL) && bytes == sizeof(msg)) {
        // processa msg (entrar, palavra, pontuação, etc)
        // e responde com WriteFile(hPipe, &resp, sizeof(resp), &bytes, NULL);
    }

    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);
    return 0;
}


int _tmain(int argc, LPTSTR argv[]) {

    // Configura consola Unicode
#ifdef UNICODE
    _setmode(_fileno(stdin), _O_WTEXT);
    _setmode(_fileno(stdout), _O_WTEXT);
#endif

    _ftprintf(stderr, TEXT("ADMIN ON.\n"));


    hShutdownEvent = CreateEvent(NULL, TRUE, FALSE, TEXT("Global\\SO2_ShutdownEvent"));
    if (!hShutdownEvent) {
        _tprintf(TEXT("[ERRO] não conseguiu criar o evento de shutdown\n"));
        return 1;
    }

    // --- 2) Lança a thread de adm (Com esta thread é possivel escrever comandos na consola)
    DWORD adminTid;
    HANDLE hAdminThread = CreateThread(
        NULL,               // Segurança padrão
        0,                  // Tamanho da stack
        AdminThread,        // Função
        NULL,               // Parâmetro
        0,                  // Flags
        &adminTid
    );

    if (!hAdminThread) {
        _tprintf(TEXT("[ERRO] não conseguiu criar AdminThread\n"));
        return 1;
    }


    // Garante instância única do árbitro
    HANDLE hMutex = CreateMutex(NULL, TRUE, ARBITRO_MUTEX_NAME);
    if (!hMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        _tprintf(TEXT("[ERRO] Já existe uma instância do árbitro.\n"));
        return 1;
    }


    // Criar e inicializar memória partilhada
    HANDLE hMap = CreateFileMapping(
        INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
        0, sizeof(MemoriaPartilhada), MEMORIA_PARTILHADA_NAME
    );
    if (!hMap) {
        _tprintf(TEXT("[ERRO] CreateFileMapping falhou.\n"));
        return 1;
    }

    MemoriaPartilhada* mem = (MemoriaPartilhada*)MapViewOfFile(
        hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(MemoriaPartilhada)
    );
    if (!mem) {
        _tprintf(TEXT("[ERRO] MapViewOfFile falhou.\n"));
        CloseHandle(hMap);
        return 1;
    }

	// Inicializa vetor com as letras que serao mostradas do lados dos jogadores
    for (int i = 0; i < MAXLETRAS; i++) {
        mem->letras[i] = TEXT('_');
        mem->estado[i] = 0;
    }
    mem->ultima_palavra[0] = TEXT('\0');


	// **2)** Lança a Thread que fica a gerar letras a cada    !!!!!!!! RITMO segundos
    srand((unsigned)time(NULL));

    // pré-enche as MAXLETRAS primeiras posições
    for (int i = 0; i < MAXLETRAS; ++i) {
        char nova = 'A' + (rand() % 26);
        mem->letras[i] = nova;
        mem->estado[i] = 1;
    }

    HANDLE hLetterThread = CreateThread(
        NULL, 0,
        LetterThread,
        mem,     // passa o ponteiro para a memória partilhada
        0, NULL
    );
    if (!hLetterThread) {
        _tprintf(TEXT("[ERRO] não conseguiu criar LetterThread\n"));
        // podes decidir falhar aqui ou continuar sem ritmo de letras
    }



    // Loop principal: cria uma instância por cliente
    while (WaitForSingleObject(hShutdownEvent, 0) != WAIT_OBJECT_0) {
        // 1) Cria instância do pipe
        HANDLE hPipe = CreateNamedPipe(
            ARBITRO_PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            MAX_JOGADORES, sizeof(Mensagem), sizeof(Mensagem),
            0, NULL
        );
        if (hPipe == INVALID_HANDLE_VALUE) break;

        // 2) Aguarda ligação
        BOOL ok = ConnectNamedPipe(hPipe, NULL)
            || GetLastError() == ERROR_PIPE_CONNECTED;
        if (!ok) {
            CloseHandle(hPipe);
            continue;
        }

        // 3) Despacha thread para tratar o cliente
        CLIENT_PARAM* cp = malloc(sizeof(*cp));
        cp->hPipe = hPipe;
        cp->mem = mem;
        HANDLE hThread = CreateThread(NULL, 0, ClientThread, cp, 0, NULL);
        if (hThread) CloseHandle(hThread);
        else {
            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
            free(cp);
        }
    }

    // Cleanup
    UnmapViewOfFile(mem);
    CloseHandle(hMap);
    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
    //CloseHandle(hThread);
    return 0;
}
