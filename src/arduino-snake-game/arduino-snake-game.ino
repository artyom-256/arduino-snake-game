/**
 * Copyright (c) 2021 Artem Hlumov <artyom.altair@gmail.com>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "LedControl.h"

// ====================================================
//                         PINS
// ====================================================

// LED matrix pins.
const int MATRIX_DIN_PIN = 12;
const int MATRIX_CS_PIN = 11;
const int MATRIX_CLK_PIN = 10;
// Pin of the buzzer (+).
const int BUZZER_PIN = 2;
// Joystick data pins.
const int JOYSTICK_X_PIN = A0;
const int JOYSTICK_Y_PIN = A1;
// Not connected analogue pin for generating a random seed.
const int RANDOM_NOISE_PIN = A2;

// ====================================================
//              CLASSES AND DATA STRUCTURES
// ====================================================

/**
 * Structure to represent a point on a plane.
 */
struct point
{
  int x;
  int y;

  point operator+(const point& other) const
  {
    return { this->x + other.x, this->y + other.y };
  }
  point& operator+=(const point& other)
  {
    this->x += other.x;
    this->y += other.y;
    return *this;
  }
  bool operator==(const point& other) const
  {
    return this->x == other.x && this->y == other.y;
  }
  bool operator!=(const point& other) const
  {
    return !(*this == other);
  }
};

/**
 * Direction of movement.
 */
enum Direction
{
  LEFT,
  UP,
  RIGHT,
  DOWN
};

/**
 * Create a point representing a unit vector pointing to the given direction.
 * @param dir Direction of the vector.
 * @return Unit vector.
 */
point makeDirectionVector(Direction dir)
{
  switch (dir) {
    case LEFT:
      return {-1, 0};
    case UP:
      return {0, -1};
    case RIGHT:
      return {1, 0};
    case DOWN:
      return {0, 1};
  }
}

/**
 * Wrapper around LedControl that implements buffering.
 * So we can first set up the state of the matrix and draw it in one pass.
 */
class Matrix {
  public:
    /**
     * Constructor.
     * @param rows Amount of rows of the matrix.
     * @param rows Amount of columns of the matrix.
     */
    Matrix(int rows, int columns)
    : m_lc(LedControl(MATRIX_DIN_PIN, MATRIX_CLK_PIN, MATRIX_CS_PIN, 1))
    , m_rows(rows)
    , m_columns(columns)
    , m_display(new int[rows])
    {
      // MAX72XX is is power-saving mode, so we should swtch it on first.
      m_lc.shutdown(0, false);
      // Set up brightness.
      m_lc.setIntensity(0, 8);
      // Clean up display.
      m_lc.clearDisplay(0);
      // Clean up the internal state to synchronize it with the dispaly.
      clear();
    }
    /**
     * Destructor.
     */
    ~Matrix()
    {
      delete[] m_display;
    }
    /**
     * Change state of the particular pixel of the matrix.
     * The method does not apply changes immediately, but only modifies buffer.
     * To apply changes to the matrix use flush().
     * @param p Point to change.
     * @param value State of the pixed (true - switch on, false - switch off).
     */
    void setPixel(const point& p, bool value)
    {
      // Calculate a mask that has a bit 1 on the position that corresponds
      // to the LED position (according to its x coordinate).
      int mask = 1 << (m_rows - p.x - 1);
      if (value) {
        // Set the bit to 1.
        m_display[p.y] |= mask;
      } else {
        // Set the bit to 0.
        m_display[p.y] &= ~mask;
      }
    }
    /**
     * Set state of all LEDs to switched off (0).
     * The method does not apply changes immediately, but only modifies buffer.
     * To apply changes to the matrix use flush().
     */
    void clear()
    {
      memset(m_display, 0, sizeof(m_display[0]) * m_rows);
    }
    /**
     * Apply state to the matrix.
     */
    void flush()
    {
      for (int i = 0; i < m_rows; i++) {
        m_lc.setRow(0, i, m_display[i]);
      }
    }
    /**
     * Return amount of rows of the matix.
     * @retrn Count rows.
     */
    int getRowsCount() const
    {
      return m_rows;
    }
    /**
     * Return amount of columns of the matix.
     * @retrn Count columns.
     */
    int getColumnsCount() const
    {
      return m_columns;
    }

  private:
    /**
     * Object to control the LED matrix.
     */
    LedControl m_lc;
    /**
     * Number of rows.
     */
    int m_rows;
    /**
     * Number of columns.
     */
    int m_columns;
    /**
     * Define a display state. Each element of the array represents one row.
     * Bites of the element represent values of particular LED on the row (from higher to lower bits).
     * Bit 1 represent a switched on LED.
     */
    int* m_display;
};

/**
 * Represent a snake positioned on the canvas (Matrix).
 * Snake movement is split into two fucntions: extend() that promotes the head and cut() that removes the tail.
 * So we can just omit cut() call in order to emulate snake growing.
 */
class Snake {
  public:
    /**
     * Initialize the snake and put it to the start position.
     * @param canvas Matrix that will be used as a canvas for drawing the snake.
     * @param initLength Start length of the snake.
     * @param maxLength Maximal length of the snake.
     * @param start Start point where the end of the tail will be placed.
     * @param dir Direction of the snake.
    */
    Snake(Matrix* canvas, int initLength, int maxLength, const point& start, int dir)
    : m_canvas(canvas)
    , m_currentLength(initLength)
      // Keep one more element to be able to execute last extend() call before game over.
    , m_snake(new point[maxLength + 1])
    {
      // Create a unit direction vector to construct the snake.
      point directionVector = makeDirectionVector(dir);
      // Construct the snake and draw it on the canvas.
      struct point currPos = start;
      for (int i = 0; i < m_currentLength; i++) {
        m_snake[i] = currPos;
        m_canvas->setPixel(m_snake[i], true);
        // Grow the snake in the given diection.
        currPos += directionVector;
      }
    }
    /**
     * Destructor.
     */
    ~Snake()
    {
      delete[] m_snake;
    }
    /**
     * First part of the snake movement: extend the snake to the given direction and occupy a new
     * pixel next to the head.
     * @param dir Movement direction.
     */
    void extend(Direction dir)
    {
      // We implement the snake game on a thorus which means when the snake reaches
      // the last column, it jumps to the first one, so the game space is cycled.
      point head = getHead();
      // Shift the head to the given direction.
      head += makeDirectionVector(dir);
      // Firs of all we need to add enough big vector which is a multiple of the matrix size
      // to get rid of negative values.
      head += {m_canvas->getColumnsCount(), m_canvas->getRowsCount()};
      // Then we divide each coordinate by width or height to make the space cycled.
      head.x %= m_canvas->getColumnsCount();
      head.y %= m_canvas->getRowsCount();
      // Save new head.
      m_snake[m_currentLength] = head;
      m_currentLength++;
      // Represent the change on the canvas.
      m_canvas->setPixel(head, true);
    }
    /**
     * Second part of the snake movement.
     */
    void cut()
    {
      point tail = getTail();
      // Clean up the tail pixel in the canvas.
      m_canvas->setPixel(tail, false);
      // Shift all points left, so the tail point gets removed.
      for (int i = 0; i < m_currentLength; i++) {
        m_snake[i] = m_snake[i + 1];
      }
      // Align snake length after cutting the tail.
      m_currentLength--;
    }
    /**
     * Check if the point is occupied by the snake.
     * @param p Point to check.
     * @return Whether the point is occupoed by the snake.
     */
    bool isOccupied(const point& p) const
    {
      for (int i = 0; i < m_currentLength; i++) {
        if (p == m_snake[i]) {
          return true;
        }
      }
      return false;
    }
    /**
     * Check if the snake has self-collission.
     * The method does not work if it is called after several movements, so the collision check
     * should be performed in each step.
     * @return Whether the snake has self-collision.
     */
    bool checkSelfCollision() const
    {
      // As the head is the only pixel that moves forward in each step we can simplify the algorithm
      // and only check if the head gets into some occupied points.
      point head = getHead();
      // Go over all points ignoring the last one (the head).
      for (int i = 0; i < m_currentLength - 1; i++) {
        if (m_snake[i] == head) {
          return true;
        }
      }
      return false;
    }
    /**
     * Return a head point.
     * @return Head point.
     */
    const point& getHead() const
    {
      return m_snake[m_currentLength - 1];
    }
    /**
     * Return a tail point.
     * @return Tail point.
     */
    const point& getTail() const
    {
      return m_snake[0];
    }
    /**
     * Return snake length in points.
     * @return Snake length.
     */
    int getLength() const
    {
      return m_currentLength;
    }

  private:
    /**
     * Canvas that is used to represent the snake.
     */
    Matrix* m_canvas;
    /**
     * Current length of the snake in pixels.
     * If the snake reaches MAX_SNAKE_LENGTH the player wins.
     */
    int m_currentLength;
    /**
     * Array of points representing position of the snake on the playfield.
     * Index 0 corresponds to a tail, index 
     */
    point* m_snake;
};

/**
 * Represent food available on the game field.
 */
class Food
{
  public:
    /**
     * Constructor.
     * @param canvas Matrix that is used to draw food.
     * @param maxFoodCount Maximal amount of food pieces.
     */
    Food(Matrix* canvas, int maxFoodCount)
    : m_canvas(canvas)
    , m_foodPoints(new point[maxFoodCount])
    , m_foodCount(0)
    {
    }
    /**
     * Destructor.
     */
    ~Food()
    {
      delete[] m_foodPoints;
    }
    /**
     * Place food pieces to a non occupied points of the canvas.
     * @param snake Snake that defines a set of occupied points where the food should not appear.
     * @param numToPlace Amount of food pieces to place.
     */
    void placeFood(const Snake* snake, int numToPlace)
    {
      while (true) {
        // Select a random point.
        point p = {random(m_canvas->getColumnsCount()), random(m_canvas->getRowsCount())};
        // Check if it is occupied by the snake or another food pieces.
        if (!snake->isOccupied(p) && !isOccupied(p)) {
          // Place the food.
          m_foodPoints[m_foodCount] = p;
          m_foodCount++;
          numToPlace--;
          // Draw the food.
          m_canvas->setPixel(p, true);
          // Jump out if we have placed all the food.
          if (numToPlace <= 0) {
            break;
          }
        }
      }
    }
    /**
     * Try to eat a food on the given point.
     * @param p Point to try.
     * @return True if the food has been eaten and false otherwise.
     */
    bool tryEat(const point& p)
    {
      // Go through all food pieces.
      for (int i = 0; i < m_foodCount; i++) {
        if (m_foodPoints[i] == p) {
          // Once we have found a piece that is places on the given point,
          // swap it with the last element in the array and remove this last element,
          // so we do not need to shift other elements in the array.
          m_foodPoints[i] = m_foodPoints[m_foodCount - 1];
          // Do not wipe the element, simply forget about it.
          m_foodCount--;
          // Assuming there is no overlapping food as we have foind one
          // intersection, there can't be more.
          return true;
        }
      }
      return false;
    }
    /**
     * Return amount of remaining food points.
     * @return Amount of remaining food points.
     */
    int getCount() const
    {
      return m_foodCount;
    }

  private:
    /**
     * Check if the point is occupied by the food.
     */
    bool isOccupied(const point& p) const
    {
      for (int i = 0; i < m_foodCount; i++) {
        if (m_foodPoints[i] == p) {
          return true;
        }
      }
      return false;
    }
    /**
     * Canvas that is used to represent the food points.
     */
    Matrix* m_canvas;
    /**
     * Array of occupied points.
     * Size of the array is m_foodCount, maximal capacity is m_maxFoodCount.
     */
    point* m_foodPoints;
    /**
     * Amount of food pieces currently placed on the canvas.
     */
    int m_foodCount;
};

// ====================================================
//                   GAME PARAMETERS
// ====================================================

/**
 * Height of LED matrix.
 */
const int ROWS = 8;
/**
 * Width of the LED matrix.
 */
const int COLUMNS = 8;
/**
 * Initial snake direction.
 */
Direction direct = DOWN;
/**
 * Maximal length of the snake in pixels.
 */
const int MAX_SNAKE_LENGTH = 12;
/**
 * Initial size of the snake including blinking head.
 */
const int INIT_SNAKE_LENGTH = 2;
/**
 * Total amount of food points places on the play field.
 */
const int FOOD_POINTS = 2;
/**
 * Define whether there should always be FOOD_POINTS food pieces on the play field (true),
 * or the pieces should be refilled after eating the last one (false).
 */
const bool REFILL_IMMEDIATELY = true;
/**
 * Threshold of joystick movement to be considered as a user input (in range 0 - 512).
 */
const int JOYSTICK_THRESHOLD = 50;
/**
 * Define how many time the head should blink during one step.
 * Blink means the LED switches on and off.
 */
const int BLINK_FREQUENCY = 2;
/**
 * Deplay between sneak movements.
 * Smaller value makes the sneak faster.
 */
const int STEP_DELAY = 200;

// ====================================================
//                    GAME VARIABLES
// ====================================================

/**
 * Canvas to draw on.
 */
Matrix* matrix = nullptr;
/**
 * Snake object controllable by the player.
 */
Snake* snake = nullptr;
/**
 * Food positions.
 */
Food* food = nullptr;

// ====================================================
//                    GAME FUNCTIONS
// ====================================================

/**
 * Initialize or re-initialize the game.
 */
void initGame() {
  // Clean up old objects.
  if (matrix) {
    delete matrix;
  }
  if (snake) {
    delete snake;
  }
  if (food) {
    delete food;
  }
  // Create new objects.
  matrix = new Matrix(ROWS, COLUMNS);
  snake = new Snake(matrix, INIT_SNAKE_LENGTH, MAX_SNAKE_LENGTH, {COLUMNS - 1, 0}, direct);
  food = new Food(matrix, FOOD_POINTS);
}

/**
 * Read diection reported by the joystick.
 * @param defaultDirection Direction to be returned if the joystick is in the central position meaning no direction changes needed.
 */
int readDirection(Direction defaultDirection)
{
  // Read x and y directions (analogueRead() returns value from 0 to 1023 where ~512 is a center).
  long xPin = analogRead(JOYSTICK_X_PIN) - 512;
  long yPin = analogRead(JOYSTICK_Y_PIN) - 512;
  // Calculate square of length (do not use length to avoid sqrt()).
  long inputLength = xPin * xPin + yPin * yPin;
  // Ignore too small deviations. Joystick requires individual calibration to be more precise, but we
  // can work it around by ignoring too small movements.
  // As we do non need to be too precise here and want to avoid using sqrt() in embedded, we compare square of length.
  // If JOYSTICK_THRESHOLD is relatively small comparing to the range 0-512, this is enough fair.
  if (inputLength > JOYSTICK_THRESHOLD * JOYSTICK_THRESHOLD) {
    // You may need to change values here depending on how the matrix is soldered comparing to the joystick
    // to preserve natural meaning of directions to the user.
    if (abs(xPin) > abs(yPin)) {
      if (xPin > 0) {
        return LEFT;
      } else {
        return RIGHT;
      }
    } else {
      if (yPin > 0) {
        return UP;
      } else {
        return DOWN;
      }
    }
  }
  return defaultDirection;
}

/**
 * Play curtain animation before starting a new game.
 */
void curtainAnimation()
{
  for (int i = 0; i < matrix->getRowsCount(); i++) {
    for (int j = 0; j < matrix->getColumnsCount(); j++) {
      // Clean up the previous row (if exists).
      if (i != 0) {
        matrix->setPixel({j, i - 1}, false);
      }
      // And light up the current one.
      matrix->setPixel({j, i}, true);
    }
    matrix->flush();
    delay(100);
  }
}

/**
 * Play loose sound.
 */
void playLose()
{
  // Play a melody.
  digitalWrite(2, HIGH);
  delay(500);
  digitalWrite(2, LOW);
  // Start new game.
  curtainAnimation();
  initGame();
}

/**
 * Play win sound.
 */
void playWin()
{
  // Play a melody.
  for (int i = 0; i < 10; i++) {
    digitalWrite(2, HIGH);
    delay(50);
    digitalWrite(2, LOW);
    delay(50);
  }
  // Start new game.
  curtainAnimation();
  initGame();
}

// ====================================================
//               SETUP AND MAIN LOOP
// ====================================================

void setup() {
  // Set up a pin connected to the buzzer.
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  // Initialize random generator with a noise on a not connected analogue pin.
  randomSeed(analogRead(RANDOM_NOISE_PIN));
  // Initialize game variables.
  initGame();
  // Place the food on the playfield.
  food->placeFood(snake, FOOD_POINTS);
}

void loop() {
  // Read direction from the joystick.
  int newDirect = readDirection(direct);
  // Do not allow to change direction to opposite, e.g. UP to DOWN and LEFT to RIGHT.
  // As direction constants are represented in the clockwise order this is equal
  // to the statement that difference between new and old direction should be odd.
  if ((newDirect - direct) % 2) {
    direct = newDirect;
  }
  // Do the first part of the snake movement: extent it to occupy a cell next to the head.
  snake->extend(direct);
  point head = snake->getHead();
  // Check if the head hits the food.
  // If the food is not eaten we should complete the second part of movement and cut the tail.
  // If the food is eaten - just skip cutting which means the snake will extend.
  if (food->tryEat(snake->getHead())) {
    // Play a beep.
    digitalWrite(BUZZER_PIN, HIGH);
  } else {
    snake->cut();
  }
  // Refill food if need.
  if (REFILL_IMMEDIATELY) {
    // Always keep FOOD_POINTS food pieces on the playfied.
    if (food->getCount() < FOOD_POINTS) {
      food->placeFood(snake, FOOD_POINTS - food->getCount());
    }
  } else {
    // Only refill if the food is over.
    if (food->getCount() == 0) {
      food->placeFood(snake, FOOD_POINTS);
    }
  }
  // Make the changes visible.
  matrix->flush();
  // Check lose condition (if the head is hit into the snake body).
  if (snake->checkSelfCollision()) {
    playLose();
    return;
  }
  // Check win condition.
  if (snake->getLength() == MAX_SNAKE_LENGTH) {
    playWin();
    return;
  }
  // Run a delay.
  // Half blink is a time period when LED is ether switched on or off, so one blink
  // consists of 2 half-blinks.
  const int NUM_HALF_BLINKS = 2 * BLINK_FREQUENCY;
  // Calculate time period for each half-blink taking into account that we should
  // do BLINK_FREQUENCY blinks during STEP_DELAY.
  const int HALF_BLINK_DELAY = STEP_DELAY / NUM_HALF_BLINKS;
  // We have a cycle and a boolean variable that defines whether the LED is on or off
  // and switched every loop. So the cycle loop represents a half-blink and
  // the cycle should run NUM_HALF_BLINKS loops.
  bool isHeadVisible = false;
  for (int i = 0; i < NUM_HALF_BLINKS; i++) {
    // Set the head pixel value.
    matrix->setPixel(snake->getHead(), isHeadVisible);
    // Make it visible.
    matrix->flush();
    // Switch to another state.
    isHeadVisible = !isHeadVisible;
    // Wait a delay so after the cycle finishes we have exactly STEP_DELAY timeout.
    delay(HALF_BLINK_DELAY);
  }
  // Cancel beep if it was initiated earlier.
  digitalWrite(BUZZER_PIN, LOW);
}
