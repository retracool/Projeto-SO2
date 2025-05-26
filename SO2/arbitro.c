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

int _tmain(void)
{
    // 0) Instância única
    HANDLE hMutex = CreateMutex(NULL, TRUE, ARBITRO_MUTEX_NAME);
    if (!hMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        _ftprintf(stderr, TEXT("[ERRO] Já existe uma instância do árbitro.\n"));
        return 1;
    }

    // 1) Memória partilhada
    HANDLE hMap = CreateFileMapping(
        INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
        sizeof(MemoriaPartilhada), MEMORIA_PARTILHADA_NAME
    );
    if (!hMap) {
        _ftprintf(stderr, TEXT("[ERRO] CreateFileMapping falhou: %u\n"), GetLastError());
        ReleaseMutex(hMutex); CloseHandle(hMutex);
        return 1;
    }
    MemoriaPartilhada* mem = (MemoriaPartilhada*)MapViewOfFile(
        hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(MemoriaPartilhada)
    );
    if (!mem) {
        _ftprintf(stderr, TEXT("[ERRO] MapViewOfFile falhou: %u\n"), GetLastError());
        CloseHandle(hMap); ReleaseMutex(hMutex); CloseHandle(hMutex);
        return 1;
    }
    for (int i = 0; i < MAXLETRAS; i++) {
        mem->letras[i] = TEXT('_');
        mem->estado[i] = 0;
    }
    strcpy(mem->ultima_palavra, "");


    _tprintf(TEXT("[ÁRBITRO] Memória partilhada inicializada.\n"));

    // 2) Named pipe
    HANDLE hPipe = CreateNamedPipe(
        ARBITRO_PIPE_NAME,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        MAX_JOGADORES,
        sizeof(Mensagem), sizeof(Mensagem),
        0, NULL
    );
    if (hPipe == INVALID_HANDLE_VALUE) {
        _ftprintf(stderr, TEXT("[ERRO] CreateNamedPipe falhou: %u\n"), GetLastError());
        UnmapViewOfFile(mem); CloseHandle(hMap);
        ReleaseMutex(hMutex); CloseHandle(hMutex);
        return 1;
    }
    _tprintf(TEXT("[ÁRBITRO] Aguardando ligações em %s\n"), ARBITRO_PIPE_NAME);

    // 3) Evento de shutdown + admin thread
    hShutdownEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    CreateThread(NULL, 0, AdminThread, NULL, 0, NULL);

    // 4) Loop principal de clientes
    while (WaitForSingleObject(hShutdownEvent, 0) == WAIT_TIMEOUT) {
        BOOL connected = ConnectNamedPipe(hPipe, NULL)
            || GetLastError() == ERROR_PIPE_CONNECTED;
        if (!connected) {
            _ftprintf(stderr, TEXT("[ERRO] ConnectNamedPipe falhou: %u\n"), GetLastError());
            break;
        }
        _tprintf(TEXT("[ÁRBITRO] Cliente conectado.\n"));

        // 5) Atender o cliente (simplesmente lemos e descartamos)
        Mensagem msg;
        DWORD   bytes;
        while (ReadFile(hPipe, &msg, sizeof(msg), &bytes, NULL) && bytes == sizeof(msg)) {
            _tprintf(TEXT("[ÁRBITRO] Msg tipo=%d user=%s conteudo=%s\n"),
                msg.tipo, msg.username, msg.conteudo);
            // aqui iria o switch(msg.tipo) real…
        }

        DisconnectNamedPipe(hPipe);
        _tprintf(TEXT("[ÁRBITRO] Cliente desconectado.\n"));
    }

    // 6) Cleanup
    _tprintf(TEXT("[ÁRBITRO] Encerrando...\n"));
    UnmapViewOfFile(mem);
    CloseHandle(hMap);
    CloseHandle(hPipe);
    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
    CloseHandle(hShutdownEvent);
    return 0;
}
