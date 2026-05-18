/*
Copyright (c) 2016 Marcus Olsson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <arduboy.h>
#include <arduboy_tunes.h>
#include <arduino_compat.h>

// Forward declarations
void setupNumbers();
void drawScore();
void drawTimer();
void enterAnswer();
void setupNumbers();
void drawNumbers();
void submit();
void callEnd();
void resetInput();
void tickTime();

Arduboy arduboy;
ArduboyTunes tunes;

// Constants
#define FRAMERATE 10
#define DIFFICULTY 10

// Variables
unsigned int score = 0;                 // The score
unsigned int numbers[3] = {0, 0, 0};    // The numbers
unsigned int result = 0;                // The final calculation
int answer[3] = {0, 0, 0};              // The answer
unsigned int answerResult = 0;          // The complete answer result
char answerIndex = 0;                   // The index/position of the input
unsigned int timer_tenths = 99;         // The time left to solve (in tenths of seconds)
char text_buffer[16];                   // Text buffer storage
boolean allowInput = true;              // If the input has yet been rendered


/*
 * Setup
 */
void setup() {
  arduboy.begin();
  arduboy.setFrameRate(FRAMERATE);

  tunes.initChannel(PIN_SPEAKER_1);
  tunes.initChannel(PIN_SPEAKER_2);

  timer_tenths = 99; // 9.9 seconds
  setupNumbers();
  resetInput();
}

/*
 * Main loop
 */
void loop() {
  if(!arduboy.nextFrame()) {
    return;
  }

  allowInput = true;

  // Clear screen
  arduboy.clear();

  // Draw score
  drawScore();

  // Draw numbers
  drawNumbers();

  // Draw and "tick" timer
  tickTime();
  drawTimer();

  // Enter answer
  enterAnswer();

  arduboy.display();
}

/*
 * Draw score
 */
void drawScore() {
  sprintf(text_buffer, "SCORE:%u", score);
  arduboy.setTextSize(1);
  arduboy.setCursor(0, 0);
  arduboy.print(text_buffer);
}

/*
 * Draw the timer
 */
void drawTimer() {
  sprintf(text_buffer,"TIME:%d.%d", timer_tenths / 10, timer_tenths % 10);
  arduboy.setTextSize(1);
  arduboy.setCursor(80, 0);
  arduboy.print(text_buffer);
}

/*
 * Input for the players answer
 */
void enterAnswer() {

  if(allowInput == true) {

    // Disable input to prevent double input
    allowInput = false;

    if (arduboy.pressed(LEFT_BUTTON) || arduboy.pressed(B_BUTTON)) {
      answerIndex--;
      if (answerIndex < 0) {
        answerIndex = 0;
      } else {
        // tunes.tone(1046, 250);
      }
    }

    if (arduboy.pressed(RIGHT_BUTTON)) {
      answerIndex++;
      if (answerIndex > 2) {
        answerIndex = 2;
      } else {
        // tunes.tone(1046, 250);
      }
    }

    if (arduboy.pressed(DOWN_BUTTON)) {
      answer[answerIndex]--;
      // tunes.tone(523, 250);
      if (answer[answerIndex] < 0) {
        answer[answerIndex] = 9;
      }
    }

    if (arduboy.pressed(UP_BUTTON)) {
      answer[answerIndex]++;
      // tunes.tone(523, 250);
      if (answer[answerIndex] > 9) {
        answer[answerIndex] = 0;
      }
    }

    // Set answer
    answerResult = (answer[0] * 100) + (answer[1] * 10) + answer[2];


    submit(); // <-- Direct submit

    /*
     * @todo set input to button press
     */

    if (arduboy.pressed(A_BUTTON)) {
      if (answerIndex < 2) {
        answerIndex++;
        // tunes.tone(1046, 250);
      } else {
        // tunes.tone(1046, 250);
        return;
      }
    }
  }

  // Draw marker
  if(answerIndex == 0) {
    arduboy.fillCircle(51, 46, 1, 1);
  } else if(answerIndex == 1) {
    arduboy.fillCircle(65, 46, 1, 1);
  } else if(answerIndex == 2) {
    arduboy.fillCircle(79, 46, 1, 1);
  }

  // Output answer variables
  arduboy.setTextSize(2);

  sprintf(text_buffer, "%u", answer[0]);
  arduboy.setCursor(46, 50);
  arduboy.print(answer[0]);

  sprintf(text_buffer, "%u", answer[1]);
  arduboy.setCursor(60, 50);
  arduboy.print(answer[1]);

  sprintf(text_buffer, "%u", answer[2]);
  arduboy.setCursor(74, 50);
  arduboy.print(answer[2]);
}

/*
 * Randomly generate numbers to calculate
 */
void setupNumbers() {
  result = 0;

  numbers[0] = rand() % DIFFICULTY;
  numbers[1] = rand() % DIFFICULTY;

  numbers[2] = numbers[0] + numbers[1];

  while(numbers[2] >= numbers[0] + numbers[1]) {
    numbers[2] = rand() % DIFFICULTY;
  }

  result = numbers[0] + numbers[1] - numbers[2];
}

/*
 * Draw the numbers to the view
 */
void drawNumbers() {
  sprintf(text_buffer, "%u+%u-%u=?", numbers[0], numbers[1], numbers[2]);
  arduboy.setTextSize(2);
  arduboy.setCursor(0, 15);
  arduboy.print(text_buffer);
}

/*
 * Take and "Correct" the users answer
 */
void submit() {
  // Compare results
  if(result == answerResult) {
    score += 10;
    drawNumbers();
    timer_tenths = 99; // 9.9 seconds
    setupNumbers();
    resetInput();
  }
}

/*
 * End game
 * @todo: End the game for real, right now infinite loop
 */
void callEnd() {
  timer_tenths = 99; // 9.9 seconds
  setupNumbers();
  resetInput();
}

/*
 * Reset to 000
 */
void resetInput() {
  answer[0] = 0;
  answer[1] = 0;
  answer[2] = 0;

  answerIndex = 0;
}

/*
 * Countdown timer each cycle
 */
void tickTime() {
  // Decrease timer by 1 tenth of second per frame (at 10 FPS)
  if(timer_tenths > 0) {
    timer_tenths--;
  }
  if(timer_tenths == 0) {
    callEnd();
  }
}
