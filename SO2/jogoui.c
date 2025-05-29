#define _CRT_SECURE_NO_WARNINGS
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <tchar.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <string.h>
#include "estrutura.h"

static HANDLE g_hPipe = INVALID_HANDLE_VALUE;
static TCHAR  g_username[MAX_USERNAME];


BOOL WINAPI CtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_CLOSE_EVENT) {
        if (g_hPipe != INVALID_HANDLE_VALUE) {
            Mensagem m = { MSG_SAIR, {0}, {0}, 0 };
            _tcsncpy(m.username, g_username, MAX_USERNAME - 1);
            DWORD written;
            WriteFile(g_hPipe, &m, sizeof(m), &written, NULL);
            // opcional: esperar um pouco para garantir envio
            Sleep(100);
            CloseHandle(g_hPipe);
        }
        ExitProcess(0);
        return TRUE;
    }
    return FALSE;
}


int _tmain(int argc, LPTSTR argv[]) {

    // logo no início do _tmain(...)
    SetConsoleCtrlHandler(CtrlHandler, TRUE);


    // Configura consola Unicode
#ifdef UNICODE
    _setmode(_fileno(stdin), _O_WTEXT);
    _setmode(_fileno(stdout), _O_WTEXT);
#endif



    if (argc < 2) {
        _tprintf(TEXT("Uso: %s <username>\n"), argv[0]);
        return 1;
    }

    // Copia username
    TCHAR username[MAX_USERNAME];
    _tcsncpy(username, argv[1], MAX_USERNAME - 1);
    username[MAX_USERNAME - 1] = TEXT('\0');
    _tcsncpy(g_username, username, MAX_USERNAME - 1);
    g_username[MAX_USERNAME - 1] = TEXT('\0');
    // Aguarda pipe com timeout
    if (!WaitNamedPipe(ARBITRO_PIPE_NAME, 5000)) { // 5 segundos timeout
        DWORD error = GetLastError();
        if (error == ERROR_SEM_TIMEOUT) {
            _tprintf(TEXT("[JOGADOR] Árbitro não responde (timeout).\n"));
        } else {
            _tprintf(TEXT("[JOGADOR] Árbitro não está ativo.\n"));
        }
        return 1;
    }

    // Conecta
    HANDLE hPipe = CreateFile(
        ARBITRO_PIPE_NAME,
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL
    );
    if (hPipe == INVALID_HANDLE_VALUE) {
        _tprintf(TEXT("[JOGADOR] Falha ao conectar ao árbitro.\n"));
        return 1;
    }

    // ISTO É PARA CASO SAIA BRUTAMENTE O JOGADOR
    g_hPipe = hPipe;
   

    // Envia MSG_ENTRAR
    Mensagem msg = { 0 };
    msg.tipo = MSG_ENTRAR;
    _tcsncpy(msg.username, username, MAX_USERNAME - 1);
    DWORD written;
    WriteFile(hPipe, &msg, sizeof(msg), &written, NULL);

    //_tprintf(TEXT("DEBUG: MSG_ENTRAR = %d\n"), MSG_ENTRAR);

	// Recebe resposta para ver se foi aceite ou nao pea o árbitro

    Mensagem resp;
    DWORD readBytes;
    if (ReadFile(hPipe, &resp, sizeof(resp), &readBytes, NULL) && readBytes == sizeof(resp)) {
        _tprintf(TEXT("[ÁRBITRO] %s\n"), resp.conteudo);
        if (resp.tipo != MSG_SUCESSO) {
            CloseHandle(hPipe);
            return 0;   // ou exit(0);
        }
        if (resp.tipo == MSG_PONTUACAO) {
            _tprintf(TEXT(" (pontos: %.1f)"), resp.pontuacao);
        }
    }
    else {
        _tprintf(TEXT("[JOGADOR] Conexão encerrada pelo árbitro.\n"));
        return 1;
    }

    // 2) Tenta abrir a memória partilhada para aceder as letras do arbitro.c

    HANDLE hMap = OpenFileMapping(
        FILE_MAP_READ,      // só leitura
        FALSE,              // não herda handle
        MEMORIA_PARTILHADA_NAME
    );
    if (!hMap) {
        _tprintf(TEXT("Erro: Memória partilhada não existe. O árbitro deve estar a correr.\n"));
        return 1;
    }

    // 3) Mapeia para a nossa estrutura
    MemoriaPartilhada* mem = (MemoriaPartilhada*)MapViewOfFile(
        hMap, FILE_MAP_READ, 0, 0, sizeof(MemoriaPartilhada)
    );
    if (!mem) {
        _tprintf(TEXT("Erro: não foi possível mapear a memória partilhada.\n"));
        CloseHandle(hMap);
        return 1;
    }


    // 4) Lê e imprime as letras visíveis
    _tprintf(TEXT("Letras visíveis:\n"));
    for (int i = 0; i < MAXLETRAS; ++i) {
        // Só mostra letras que estão disponíveis (estado = 1)
        if (mem->estado[i] == 1) {
            _tprintf(TEXT("%c "), mem->letras[i]);
        }
    }
    _tprintf(TEXT("\n"));


   
    // Loop de I/O
    TCHAR linha[256];
    BOOL running = TRUE;

    while (running && _fgetts(linha, 256, stdin)) {
        linha[_tcslen(linha) - 1] = TEXT('\0');
        if (linha[0] == TEXT('\0')) continue;

        ZeroMemory(&msg, sizeof(msg));
        _tcsncpy(msg.username, username, MAX_USERNAME - 1);

        if (linha[0] == TEXT(':')) {
            if (_tcscmp(linha, TEXT(":pont")) == 0) {
                msg.tipo = MSG_PONTUACAO;
            }
            else if (_tcscmp(linha, TEXT(":jogs")) == 0) {
                msg.tipo = MSG_JOGADORES;
            }
            else if (_tcscmp(linha, TEXT(":sair")) == 0) {
                msg.tipo = MSG_SAIR;
                running = FALSE;
            }
            else {
                _tprintf(TEXT("[JOGADOR] Comando desconhecido: %s\n"), linha);
                continue;
            }
        }
        else {
            msg.tipo = MSG_PALAVRA;
            _tcsncpy(msg.conteudo, linha, MAX_PALAVRA - 1);
        }

        WriteFile(hPipe, &msg, sizeof(msg), &written, NULL);

        // Recebe resposta
        if (!ReadFile(hPipe, &resp, sizeof(resp), &readBytes, NULL)
            || readBytes != sizeof(resp)) {
            _tprintf(TEXT("[JOGADOR] Conexão encerrada pelo árbitro.\n"));
            break;
        }

        // VERIFICAÇÃO DE ENCERRAMENTO
        if (resp.tipo == MSG_ERRO && _tcsstr(resp.conteudo, TEXT("Servidor a encerrar")) != NULL) {
            _tprintf(TEXT("[ÁRBITRO] %s\n"), resp.conteudo);
            _tprintf(TEXT("[JOGADOR] O jogo foi encerrado.\n"));
            break;
        }

        _tprintf(TEXT("[ÁRBITRO] %s"), resp.conteudo);
        if (resp.tipo == MSG_PONTUACAO) {
            _tprintf(TEXT(" (pontos: %.1f)"), resp.pontuacao);
        }
        _tprintf(TEXT("\n"));
    }

    UnmapViewOfFile(mem);
    CloseHandle(hPipe);
    return 0;
}
