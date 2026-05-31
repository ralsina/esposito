/*
 * Breakout - A simple Breakout clone for Arduboy
 * Public Domain - use freely
 */

#include <arduboy.h>
#include <arduboy_tunes.h>
#include <arduino_compat.h>

void setupBreakout();
void updateGame();
void drawGame();
void resetBall();
void resetBricks();
void drawLives();
void drawScore();

Arduboy arduboy;
ArduboyTunes tunes;

#define FRAMERATE 30
#define PADDLE_W 24
#define PADDLE_H 3
#define PADDLE_Y 58
#define PADDLE_SPEED 3

#define BALL_SIZE 3
#define BALL_SPEED 2

#define BRICK_ROWS 5
#define BRICK_COLS 10
#define BRICK_W 12
#define BRICK_H 4
#define BRICK_GAP 1
#define BRICK_TOP 12

#define MAX_LIVES 3

int paddle_x;
int ball_x, ball_y;
int ball_dx, ball_dy;
int score;
int lives;
bool bricks[BRICK_ROWS][BRICK_COLS];
bool game_over;
bool game_won;

void setup() {
    arduboy.begin();
    arduboy.setFrameRate(FRAMERATE);
    tunes.initChannel(PIN_SPEAKER_1);
    tunes.initChannel(PIN_SPEAKER_2);
    setupBreakout();
}

void setupBreakout() {
    score = 0;
    lives = MAX_LIVES;
    game_over = false;
    game_won = false;
    paddle_x = (WIDTH - PADDLE_W) / 2;
    resetBall();
    resetBricks();
}

void resetBall() {
    ball_x = WIDTH / 2;
    ball_y = PADDLE_Y - BALL_SIZE - 1;
    ball_dx = BALL_SPEED;
    ball_dy = -BALL_SPEED;
}

void resetBricks() {
    for (int r = 0; r < BRICK_ROWS; r++) {
        for (int c = 0; c < BRICK_COLS; c++) {
            bricks[r][c] = true;
        }
    }
}

void loop() {
    if (!arduboy.nextFrame()) return;

    if (game_over || game_won) {
        if (arduboy.pressed(A_BUTTON)) {
            setupBreakout();
        }
        arduboy.clear();
        arduboy.setTextSize(1);
        if (game_won) {
            arduboy.setCursor(20, 28);
            arduboy.print("YOU WIN!");
        } else {
            arduboy.setCursor(20, 28);
            arduboy.print("GAME OVER");
        }
        arduboy.setCursor(10, 40);
        arduboy.print("Score: ");
        arduboy.print(score);
        arduboy.setCursor(10, 50);
        arduboy.print("Press A to retry");
        arduboy.display();
        return;
    }

    // Input
    if (arduboy.pressed(LEFT_BUTTON)) {
        paddle_x -= PADDLE_SPEED;
        if (paddle_x < 0) paddle_x = 0;
    }
    if (arduboy.pressed(RIGHT_BUTTON)) {
        paddle_x += PADDLE_SPEED;
        if (paddle_x > WIDTH - PADDLE_W) paddle_x = WIDTH - PADDLE_W;
    }

    // Move ball
    ball_x += ball_dx;
    ball_y += ball_dy;

    // Wall collisions
    if (ball_x <= 0 || ball_x >= WIDTH - BALL_SIZE) {
        ball_dx = -ball_dx;
        if (ball_x < 0) ball_x = 0;
        if (ball_x > WIDTH - BALL_SIZE) ball_x = WIDTH - BALL_SIZE;
    }
    if (ball_y <= 0) {
        ball_dy = -ball_dy;
        ball_y = 0;
    }

    // Bottom - lose life
    if (ball_y >= HEIGHT) {
        lives--;
        if (lives <= 0) {
            game_over = true;
        } else {
            resetBall();
        }
    }

    // Paddle collision
    if (ball_dy > 0 &&
        ball_y + BALL_SIZE >= PADDLE_Y &&
        ball_y + BALL_SIZE <= PADDLE_Y + PADDLE_H &&
        ball_x + BALL_SIZE >= paddle_x &&
        ball_x <= paddle_x + PADDLE_W) {
        ball_dy = -ball_dy;
        ball_y = PADDLE_Y - BALL_SIZE;

        // Adjust angle based on where ball hits paddle
        int hit_pos = (ball_x + BALL_SIZE / 2) - paddle_x;
        if (hit_pos < PADDLE_W / 3) {
            ball_dx = -BALL_SPEED;
        } else if (hit_pos > PADDLE_W * 2 / 3) {
            ball_dx = BALL_SPEED;
        }
    }

    // Brick collisions
    for (int r = 0; r < BRICK_ROWS; r++) {
        for (int c = 0; c < BRICK_COLS; c++) {
            if (!bricks[r][c]) continue;

            int bx = c * (BRICK_W + BRICK_GAP);
            int by = BRICK_TOP + r * (BRICK_H + BRICK_GAP);

            if (ball_x + BALL_SIZE > bx && ball_x < bx + BRICK_W &&
                ball_y + BALL_SIZE > by && ball_y < by + BRICK_H) {
                bricks[r][c] = false;
                score += (BRICK_ROWS - r) * 10;

                // Determine bounce direction
                int ball_cx = ball_x + BALL_SIZE / 2;
                int ball_cy = ball_y + BALL_SIZE / 2;
                int brick_cx = bx + BRICK_W / 2;
                int brick_cy = by + BRICK_H / 2;

                int dx = ball_cx - brick_cx;
                int dy = ball_cy - brick_cy;

                if (dx * dx * BRICK_H * BRICK_H > dy * dy * BRICK_W * BRICK_W) {
                    ball_dx = -ball_dx;
                } else {
                    ball_dy = -ball_dy;
                }
                goto brick_done;
            }
        }
    }
    brick_done:

    // Check win
    game_won = true;
    for (int r = 0; r < BRICK_ROWS && game_won; r++) {
        for (int c = 0; c < BRICK_COLS && game_won; c++) {
            if (bricks[r][c]) game_won = false;
        }
    }

    // Draw
    arduboy.clear();
    drawGame();
    arduboy.display();
}

void drawGame() {
    // Draw bricks
    for (int r = 0; r < BRICK_ROWS; r++) {
        for (int c = 0; c < BRICK_COLS; c++) {
            if (bricks[r][c]) {
                int bx = c * (BRICK_W + BRICK_GAP);
                int by = BRICK_TOP + r * (BRICK_H + BRICK_GAP);
                arduboy.fillRect(bx, by, BRICK_W, BRICK_H, 1);
            }
        }
    }

    // Draw paddle
    arduboy.fillRect(paddle_x, PADDLE_Y, PADDLE_W, PADDLE_H, 1);

    // Draw ball
    arduboy.fillCircle(ball_x + BALL_SIZE / 2, ball_y + BALL_SIZE / 2, BALL_SIZE / 2, 1);

    // Draw score
    drawScore();

    // Draw lives
    drawLives();
}

void drawScore() {
    arduboy.setTextSize(1);
    arduboy.setCursor(0, 0);
    arduboy.print("S:");
    arduboy.print(score);
}

void drawLives() {
    arduboy.setTextSize(1);
    arduboy.setCursor(80, 0);
    arduboy.print("L:");
    arduboy.print(lives);
}
