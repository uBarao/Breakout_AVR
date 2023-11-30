/**
 * Trabalho Pratico 2 - Jogo Eletrônico
 * Programacao de Baixo Nivel - Turma 011 - 2023/2
 * Prof. Edson Ifarraguirre Moreno
 * 
 * Nomes: Luca Martin Manfroi (22200922)
 *        Lucas Martins Weiss (22200310)
 *        Robson Lopes Uszacki (22204009)
 * Data: 30/11/2023
 * 
 * O jogo deverá ser proposto pelos alunos. Apesar de parecer bastante amplo, há algumas restrições que devem ser
 * consideradas no momento de definir o que será desenvolvido:
 * 
 * Restrição 1: não poderá ser desenvolvido um jogo de tabuleiro
 * Restrição 2: o seguinte conjunto de recursos deve ser utilizado:
 * - Uso de botões, keypad ou joystick como dispositivo de entrada
 * - Uso do LCD como dispositivo de saída (usar rotinas de desenho de texto, linhas e círculos)
 * - Uso de pelo menos um timer
 * - Uso de pelos menos uma interrupção (interna ou externa)
 * 
 * Funcionalidades obrigatórias:
 * - O jogo deve ter uma "tela inicial", que aguarda o pressionamento de um botão/etc para iniciar. Essa tela serve para,
 * por exemplo, gerar  um valor aleatório para sorteios durante o jogo.
 * - O jogo pode ter fim, dando vitória ao jogador ou ser do tipo que vai dificultando ao passar do tempo.
 * - O jogador tem que ser capaz de PERDER o jogo por algum critério (tempo, movimento incorreto, etc).
 * 
 * O jogo escolhido foi o Breakout, existem blocos na parte de cima do display e uma barra na parte inferior que pode
 * ser controlada pelo usuário por meio do joystick, e uma bola que se choca em todos os cantos da tela, nos blocos e
 * na barra, o objetivo é destruir todos os blocos antes de deixar a bola se chocar na parte inferior da tela.
*/

#include <stdio.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#include "nokia5110.h"

#define BOX_UP    8
#define BOX_DOWN  47
#define BOX_LEFT  0
#define BOX_RIGHT 83

#define BAR_HEIGHT      3
#define BAR_SIZE        15
#define BAR_ROW         44
#define BAR_LEFT_LIMIT  9
#define BAR_RIGHT_LIMIT 74
#define BAR_RATIO       65

#define TIMER_CLK		F_CPU / 2024
// Freq. de interrupção: 20 Hz
#define IRQ_FREQ		20

// Estrutura de bloco
struct block {
    int up;
    int down;
    int left;
    int right;
    int active;
};

int start = 1;           // O estado atual entre o jogo estar iniciado ou não
int startCount;          // O temporizador para o intervalo no inicio do jogo
int running = 0;         // O estado sobre o jogo estar rodando ou nao
int winCount = 0;        // O temporizador para o tempo que a tela de vitória ficará na tela
int points;              // Os pontos do jogador
int ballPosition[2];     // A posição atual da bola
int ballDirection[2];    // A direção que a bola esta se deslocando
int ballTrajectory[2];   // A trajetoria da bola (direção subtraindo um de cada valor a cada passagem de tempo)
int barPosition[2];      // A posição da barra (o ponto no centro)
struct block blocks[12]; // array com todos os blocos do jogo

void drawRow(int y, int xStart, int xEnd);
void drawColumn(int x, int yStart, int yEnd);
void drawBox(int up, int down, int left, int right);
void drawBlock(int up, int down, int left, int right);
void drawBlocks();
void drawBar();
int validCoords(int x, int y);
void moveBall();
void checkCollision(int newPosition[2]);
void drawBall();
void endGame();
void resetGame();
void verifyWin();

// Rotina de tratamento da interrupção do temporizador
ISR(TIMER1_COMPA_vect) {
    // Espera 30 interrupções para iniciar o jogo após dar start
    if (!start && startCount < 30) {
        startCount++;
    }
    else if (running) {
        moveBall();
    }
    if (winCount != 0) {
        winCount--;
    }
}

// Rotina de tratamento da interrupção por pressionar o botão
ISR(INT0_vect)
{
    if (start) {
        start = 0;
        startCount = 0;
        resetGame();
    }
    running = !running;
}

int main(void)
{
    // Configuração da entrada analógica (usada pelo joystick)
    DDRC = 0b00000000;
	ADMUX = 0b01100000;
	ADCSRA = 0b10000111;

    cli(); // desabilita interrupções

    DDRD &= ~(1 << PD2); // seta PD2 and PD3 como entradas
    PORTD = (1 << PD2);  // habilita pull-ups

    EICRA = (1 << ISC01) | (1 << ISC00); // interrupt sense control, borda de subida (rising edge) para INT0
    EIMSK |= (1 << INT0);                // enable INT0

	// resseta contadores para TIMER1
	TCCR1A = 0;
	TCCR1B = 0;
	TCNT1  = 0;
	// seta o registrador output compare
	OCR1A = (TIMER_CLK / IRQ_FREQ) - 1;
	// liga modo CTC
	TCCR1B |= (1 << WGM12);
	// seta CS10 e CS12 para prescaler 1024
	TCCR1B |= (1 << CS12) | (1 << CS10);  
	// habilita máscara do timer1
	TIMSK1 |= (1 << OCIE1A);

    nokia_lcd_init();
    resetGame();

    sei(); // habilita interrupções

    for (;;) {
        ADCSRA = ADCSRA | (1 << ADSC);
        
        nokia_lcd_clear();
        
        // Transforma a pontuação em uma string e mostra ela na tela
        char n[8];
        sprintf(n,"%d",points);
        nokia_lcd_set_cursor(0,0);
        nokia_lcd_write_string(n,1);

        // Desenha a caixa de fora e a barra
        drawBox(BOX_UP,BOX_DOWN,BOX_LEFT,BOX_RIGHT);
        drawBar();

        if (start) { // Se o jogo ainda não foi iniciado, espera para pressionar o botão
            if (winCount == 0) {
                nokia_lcd_set_cursor(10,20);
                nokia_lcd_write_string("PRESS START",1);
            }
            else {
                nokia_lcd_set_cursor(4,20);
                nokia_lcd_write_string("YOU WIN",2);
            }
        }
        else { // Se o jogo já tiver sido iniciado, desenha a bola e os blocos
            drawBall();
            drawBlocks();

            if (!running) { // Se não estiver em execução (pausado), printa "PAUSED"
                nokia_lcd_set_cursor(49,0);
                nokia_lcd_write_string("PAUSED",1);
            }
        }

        nokia_lcd_render();
    }
}

/**
 * drawRow:
 * 
 * y: a posição vertical onde a linha sera desenhada
 * xStart: a coluna inicial da linha
 * xEnd: a coluna final da linha
 * 
 * Desenha uma linha no lcd
*/
void drawRow(int y, int xStart, int xEnd) {
    if (xStart < xEnd) {
        for (int x = xStart; x <= xEnd; x++) {
            if (validCoords(x,y)) {
                nokia_lcd_set_pixel(x,y,1);
            }
        }
    }
    else {
        for (int x = xStart; x >= xEnd; x--) {
            if (validCoords(x,y)) {
                nokia_lcd_set_pixel(x,y,1);
            }
        }
    }
}

/**
 * drawColumn:
 * 
 * x: a posição horizontal onde a coluna sera desenhada
 * yStart: a linha inicial da coluna
 * yEnd: a linha final da coluna
 * 
 * Desenha uma coluna no lcd
*/
void drawColumn(int x, int yStart, int yEnd) {
    if (yStart < yEnd) {
        for (int y = yStart; y <= yEnd; y++) {
            if (validCoords(x,y)) {
                nokia_lcd_set_pixel(x,y,1);
            }
        }
    }
    else {
        for (int y = yStart; y >= yEnd; y--) {
            if (validCoords(x,y)) {
                nokia_lcd_set_pixel(x,y,1);
            }
        }
    }
}

/**
 * drawBox:
 * 
 * up: a posição da linha de cima da caixa
 * down: a posição da linha de baixo da caixa
 * left: a posição da coluna da esquerda da caixa
 * right: a posição da coluna da direita da caixa
 * 
 * Recebe todos os limites da caixa e desenha no lcd
*/
void drawBox(int up, int down, int left, int right) {
    drawRow(up,left,right);
    drawRow(down,left,right);
    drawColumn(left,up,down);
    drawColumn(right,up,down);
}

/**
 * drawBlock:
 * 
 * up: a posição da linha de cima do bloc
 * down: a posição da linha de baixo do bloco
 * left: a posição da coluna da esquerda do bloco
 * right: a posição da coluna da direita do bloco
 * 
 * Recebe todos os limites e desenha um bloco sólido no lcd
*/
void drawBlock(int up, int down, int left, int right) {
    for (int y = up; y <= down; y++) {
        drawRow(y, left, right);
    }
}

/**
 * drawBlocks:
 * 
 * Desenha todos os blocos ativos do jogo
*/
void drawBlocks() {
    for (int i = 0; i < 12; i++) {
        if (blocks[i].active == 1) {
            drawBlock(blocks[i].up,blocks[i].down,blocks[i].left,blocks[i].right);
        }
    }
}

/**
 * drawBar:
 * 
 * Desenha a barra dentro das delimitações definidas do jogo
*/
void drawBar() {
    int relativePos = BAR_LEFT_LIMIT + ((ADCH*BAR_RATIO)/255);
    barPosition[0] = relativePos-((BAR_SIZE-1)/2);
    barPosition[1] = relativePos+((BAR_SIZE-1)/2);
    for (int y = BAR_ROW - ((BAR_HEIGHT-1)/2); y <= BAR_ROW + ((BAR_HEIGHT-1)/2); y++) {
        drawRow(y,relativePos-((BAR_SIZE-1)/2),relativePos+((BAR_SIZE-1)/2));
    }
}

/**
 * drawBall:
 * 
 * Desenha a bola no lcd, de acordo com a posição global dela
*/
void drawBall() {
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            nokia_lcd_set_pixel(ballPosition[0]+x,ballPosition[1]+y,1);
        }
    }
}

/**
 * validCoords:
 * 
 * x: valor x da posição a ser verificada
 * y: valor y da posição a ser verificada
 * 
 * returna um booleano que é verdadeiro se a posição está dentro da caixa do jogo
*/
int validCoords(int x, int y) {
    return !(x < BOX_LEFT || x > BOX_RIGHT || y < BOX_UP || y > BOX_DOWN);
}

/**
 * moveBall:
 * 
 * Desloca a bola na direção que ela está se movendo
*/
void moveBall() {
    int newPosition[2];
    if (ballTrajectory[0] > 0) {
        newPosition[0] = ballPosition[0] + 1;
        ballTrajectory[0]--;
    }
    if (ballTrajectory[0] < 0) {
        newPosition[0] = ballPosition[0] - 1;
        ballTrajectory[0]++;
    }
    if (ballTrajectory[1] > 0) {
        newPosition[1] = ballPosition[1] + 1;
        ballTrajectory[1]--;
    }
    if (ballTrajectory[1] < 0) {
        newPosition[1] = ballPosition[1] - 1;
        ballTrajectory[1]++;
    }
    if (ballTrajectory[0] == 0 && ballTrajectory[1] == 0) {
        ballTrajectory[0] = ballDirection[0];
        ballTrajectory[1] = ballDirection[1];
    }
    checkCollision(newPosition);
}

/**
 * checkCollision:
 * 
 * newPosition: um par de pseudo x e y que representa a posição no jogo
 * 
 * Verifica se a nova posição da bola é válida, se não for, altera a nova posição, direção e trajetória da bola
*/
void checkCollision(int newPosition[2]) {
    int x = newPosition[0];
    int y = newPosition[1];

    if ((x-1) <= BOX_LEFT || (x+1) >= BOX_RIGHT) {
        ballDirection[0] = ballDirection[0] * (-1);
        ballTrajectory[0] = ballTrajectory[0] * (-1);
    }

    if ((y-1) <= BOX_UP) {
        ballDirection[1] = ballDirection[1] * (-1);
        ballTrajectory[1] = ballTrajectory[1] * (-1);
    }

    if ((y+1) >= BOX_DOWN) {
        ballDirection[1] = ballDirection[1] * (-1);
        ballTrajectory[1] = ballTrajectory[1] * (-1);
        endGame();
    }

    if (((y+1) == BAR_ROW-2 || (y+1) == BAR_ROW-1) && x >= barPosition[0]-1 && x <= barPosition[1]+1) { // colisao com a barra
        if (x <= barPosition[0] + (BAR_SIZE/3)) {
            ballDirection[0] = -1;
            ballDirection[1] = -1;
            ballTrajectory[0] = -1;
            ballTrajectory[1] = -1;
        }
        else if (x <= barPosition[1] - (BAR_SIZE/3)) {
            ballDirection[0] = 1;
            ballDirection[1] = -1;
            ballTrajectory[0] = 1;
            ballTrajectory[1] = -1;
        }
        else {
            ballDirection[1] = ballDirection[1] * (-1);
            ballTrajectory[1] = ballTrajectory[1] * (-1);
        }
    }

    if (y < 32) {
        for (int i = 0; i < 12; i++) {
            if (blocks[i].active == 1) {
                if (((x+1) == blocks[i].left || (x-1) == blocks[i].right) && y > blocks[i].up-1 && y < blocks[i].down+1) {
                    ballDirection[0] = ballDirection[0] * (-1);
                    ballTrajectory[0] = ballTrajectory[0] * (-1);
                    blocks[i].active = 0;
                    points++;
                    verifyWin();
                }
                if (((y+1) == blocks[i].up || (y-1) == blocks[i].down) && x > blocks[i].left-1 && x < blocks[i].right+1) {
                    ballDirection[1] = ballDirection[1] * (-1);
                    ballTrajectory[1] = ballTrajectory[1] * (-1);
                    blocks[i].active = 0;
                    points++;
                    verifyWin();
                }
            }
        }
    }
    ballPosition[0] = ballPosition[0] + ballDirection[0];
    ballPosition[1] = ballPosition[1] + ballDirection[1];
}

/**
 * endGame:
 * 
 * Finaliza o jogo, o jogo deixa de estar em execução e volta para o estado inicial
*/
void endGame() {
    running = 0;
    start = 1;
}

/**
 * resetGame:
 * 
 * Reseta o jogo, os pontos voltam para 0, a posição, direção e trajetória da bola voltam para as iniciais e os blocos todos são reinicializados
*/
void resetGame() {
    points = 0;

    ballPosition[0] = 41;
    ballPosition[1] = 40;
    ballDirection[0] = -1;
    ballDirection[1] = -1;
    ballTrajectory[0] = -1;
    ballTrajectory[1] = -1;

    struct block b1 = { 11, 15, 3, 21, 1 };
    blocks[0] = b1;
    struct block b2 = { 11, 15, 24, 42, 1 };
    blocks[1] = b2;
    struct block b3 = { 11, 15, 45, 62, 1 };
    blocks[2] = b3;
    struct block b4 = { 11, 15, 64, 80, 1 };
    blocks[3] = b4;
    struct block b5 = { 18, 22, 3, 21, 1 };
    blocks[4] = b5;
    struct block b6 = { 18, 22, 24, 42, 1 };
    blocks[5] = b6;
    struct block b7 = { 18, 22, 45, 62, 1 };
    blocks[6] = b7;
    struct block b8 = { 18, 22, 64, 80, 1 };
    blocks[7] = b8;
    struct block b9 = { 25, 29, 3, 21, 1 };
    blocks[8] = b9;
    struct block b10 = { 25, 29, 24, 42, 1 };
    blocks[9] = b10;
    struct block b11 = { 25, 29, 45, 62, 1 };
    blocks[10] = b11;
    struct block b12 = { 25, 29, 64, 80, 1 };
    blocks[11] = b12;
}

void verifyWin() {
    for (int i = 0; i < 12; i++) {
        if (blocks[i].active == 1) {
            return;
        }
    }
    endGame();
    winCount = 30;
}