extern "C" {
    #include <stdint.h>
    #include <stdio.h>
    #include <math.h>
    #include <libopencm3/stm32/rcc.h>
    #include <libopencm3/stm32/gpio.h>
    #include "usart/usart.h" 
    #include "sdram/sdram.h" 
    #include "gfx.h"
    #include "touch/touch.h"
    extern void clock_setup(void); 
    extern void lcd_spi_init(void);
    extern void lcd_draw_pixel(int x, int y, uint16_t color);
    extern void lcd_show_frame(void); 
}

#include "mnist_weights.h"

// Linker fixes for USART logging
extern "C" int _write(int file, char *ptr, int len) {
    (void)file;
    for (int i = 0; i < len; i++) usart_send_blocking(USART1, ptr[i]);
    return len;
}

static const int kScreenWidth = GFX_WIDTH;
static const int kScreenHeight = GFX_HEIGHT;
static const int kMnistSize = 28;
static const int kMnistDigitBox = 20;
static const int kStrokeRadius = 4;
static const int kMinStrokePixels = 8;
static const uintptr_t kSdramBase = 0xD0000000;

static const int kButtonY = 198;
static const int kButtonH = 34;
static const int kPredictButtonX = 20;
static const int kPredictButtonW = 130;
static const int kResetButtonX = 170;
static const int kResetButtonW = 130;
static const int kCanvasBottom = kButtonY - 6;

static uint8_t * const drawing_canvas = (uint8_t *)(kSdramBase + 0x80000);
static uint8_t mnist_canvas[kMnistSize * kMnistSize];
static float model_input[MNIST_INPUT_SIZE];
static float hidden0[MNIST_HIDDEN_SIZE];
static float hidden1[MNIST_HIDDEN_SIZE];
static float logits[MNIST_OUTPUT_SIZE];

static int draw_min_x = kScreenWidth;
static int draw_min_y = kScreenHeight;
static int draw_max_x = 0;
static int draw_max_y = 0;
static int stroke_pixels = 0;

static void clear_drawing_state(void) {
    for (int i = 0; i < kScreenWidth * kScreenHeight; i++) {
        drawing_canvas[i] = 0;
    }

    for (int i = 0; i < kMnistSize * kMnistSize; i++) {
        mnist_canvas[i] = 0;
    }

    draw_min_x = kScreenWidth;
    draw_min_y = kScreenHeight;
    draw_max_x = 0;
    draw_max_y = 0;
    stroke_pixels = 0;
}

static bool point_in_rect(int x, int y, int rect_x, int rect_y, int rect_w, int rect_h) {
    return x >= rect_x && x < (rect_x + rect_w) && y >= rect_y && y < (rect_y + rect_h);
}

static bool point_in_predict_button(int x, int y) {
    return point_in_rect(x, y, kPredictButtonX, kButtonY, kPredictButtonW, kButtonH);
}

static bool point_in_reset_button(int x, int y) {
    return point_in_rect(x, y, kResetButtonX, kButtonY, kResetButtonW, kButtonH);
}

static bool point_in_button_area(int x, int y) {
    return point_in_predict_button(x, y) || point_in_reset_button(x, y);
}

static bool point_in_canvas(int x, int y) {
    return x >= 0 && x < kScreenWidth && y >= 44 && y < kCanvasBottom;
}

static void draw_canvas_area(void) {
    gfx_drawRect(0, 44, kScreenWidth, kCanvasBottom - 44, GFX_COLOR_CYAN);
    gfx_drawRect(1, 45, kScreenWidth - 2, kCanvasBottom - 46, GFX_COLOR_CYAN);
}

static void draw_button(int x, int y, int w, int h, const char *label, uint16_t fill_color) {
    gfx_fillRoundRect(x, y, w, h, 5, fill_color);
    gfx_drawRoundRect(x, y, w, h, 5, GFX_COLOR_WHITE);
    gfx_setTextColor(GFX_COLOR_WHITE, fill_color);
    gfx_setCursor(x + 32, y + 11);
    gfx_puts((char *)label);
    gfx_setTextColor(GFX_COLOR_WHITE, GFX_COLOR_BLACK);
}

static void draw_controls(void) {
    gfx_fillRect(0, kCanvasBottom, kScreenWidth, kScreenHeight - kCanvasBottom, GFX_COLOR_BLACK);
    draw_button(kPredictButtonX, kButtonY, kPredictButtonW, kButtonH, "Predict", GFX_COLOR_BLUE);
    draw_button(kResetButtonX, kButtonY, kResetButtonW, kButtonH, "Reset", GFX_COLOR_RED);
}

static void show_prompt(const char *status) {
    gfx_fillScreen(GFX_COLOR_BLACK);
    gfx_setCursor(10, 8);
    gfx_setTextColor(GFX_COLOR_WHITE, GFX_COLOR_BLACK);
    gfx_puts((char *)"Draw one digit");
    gfx_setCursor(10, 24);
    gfx_puts((char *)status);
    draw_canvas_area();
    draw_controls();
    lcd_show_frame();
}

static void mark_canvas_point(int x, int y) {
    for (int dy = -kStrokeRadius; dy <= kStrokeRadius; dy++) {
        for (int dx = -kStrokeRadius; dx <= kStrokeRadius; dx++) {
            if ((dx * dx + dy * dy) > (kStrokeRadius * kStrokeRadius)) {
                continue;
            }

            int px = x + dx;
            int py = y + dy;
            if (px < 0 || px >= kScreenWidth || py < 44 || py >= kCanvasBottom) {
                continue;
            }

            int idx = py * kScreenWidth + px;
            if (drawing_canvas[idx] == 0) {
                drawing_canvas[idx] = 255;
                stroke_pixels++;
            }

            if (px < draw_min_x) draw_min_x = px;
            if (px > draw_max_x) draw_max_x = px;
            if (py < draw_min_y) draw_min_y = py;
            if (py > draw_max_y) draw_max_y = py;
        }
    }
}

static void draw_canvas_segment(int x0, int y0, int x1, int y1) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    int steps = dx < 0 ? -dx : dx;
    int abs_dy = dy < 0 ? -dy : dy;
    if (abs_dy > steps) {
        steps = abs_dy;
    }

    if (steps == 0) {
        mark_canvas_point(x0, y0);
        return;
    }

    for (int i = 0; i <= steps; i++) {
        int x = x0 + (dx * i) / steps;
        int y = y0 + (dy * i) / steps;
        mark_canvas_point(x, y);
    }
}

static void draw_stroke_segment(int x0, int y0, int x1, int y1) {
    const uint16_t color = GFX_COLOR_RED;

    gfx_drawLine(x0, y0, x1, y1, color);
    gfx_drawLine(x0 - 1, y0, x1 - 1, y1, color);
    gfx_drawLine(x0 + 1, y0, x1 + 1, y1, color);
    gfx_drawLine(x0, y0 - 1, x1, y1 - 1, color);
    gfx_drawLine(x0, y0 + 1, x1, y1 + 1, color);
    gfx_fillCircle(x0, y0, 2, color);
    gfx_fillCircle(x1, y1, 2, color);
}

static bool should_connect_points(int x0, int y0, int x1, int y1) {
    const int max_connect_distance = 50;
    int dx = x1 - x0;
    int dy = y1 - y0;

    return (dx * dx + dy * dy) <= (max_connect_distance * max_connect_distance);
}

static bool build_mnist_canvas(void) {
    if (stroke_pixels < kMinStrokePixels || draw_min_x > draw_max_x || draw_min_y > draw_max_y) {
        return false;
    }

    for (int i = 0; i < kMnistSize * kMnistSize; i++) {
        mnist_canvas[i] = 0;
    }

    int min_x = draw_min_x - 6;
    int min_y = draw_min_y - 6;
    int max_x = draw_max_x + 6;
    int max_y = draw_max_y + 6;

    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= kScreenWidth) max_x = kScreenWidth - 1;
    if (max_y >= kScreenHeight) max_y = kScreenHeight - 1;
    if (max_y >= kCanvasBottom) max_y = kCanvasBottom - 1;

    int src_w = max_x - min_x + 1;
    int src_h = max_y - min_y + 1;
    if (src_w <= 0 || src_h <= 0) {
        return false;
    }

    int dst_w = kMnistDigitBox;
    int dst_h = kMnistDigitBox;
    if (src_w > src_h) {
        dst_h = (src_h * kMnistDigitBox + src_w / 2) / src_w;
        if (dst_h < 1) dst_h = 1;
    } else {
        dst_w = (src_w * kMnistDigitBox + src_h / 2) / src_h;
        if (dst_w < 1) dst_w = 1;
    }

    int dst_x0 = (kMnistSize - dst_w) / 2;
    int dst_y0 = (kMnistSize - dst_h) / 2;

    for (int y = 0; y < dst_h; y++) {
        for (int x = 0; x < dst_w; x++) {
            int src_x = min_x + (x * src_w) / dst_w;
            int src_y = min_y + (y * src_h) / dst_h;
            mnist_canvas[(dst_y0 + y) * kMnistSize + (dst_x0 + x)] =
                drawing_canvas[src_y * kScreenWidth + src_x];
        }
    }

    return true;
}

static void draw_mnist_preview(int origin_x, int origin_y) {
    const int scale = 3;
    for (int y = 0; y < kMnistSize; y++) {
        for (int x = 0; x < kMnistSize; x++) {
            uint16_t color = mnist_canvas[y * kMnistSize + x] ? GFX_COLOR_WHITE : GFX_COLOR_BLACK;
            gfx_fillRect(origin_x + x * scale, origin_y + y * scale, scale, scale, color);
        }
    }
}

static void show_prediction_result(bool ok, int digit, int confidence) {
    gfx_fillRect(0, 0, kScreenWidth, 44, GFX_COLOR_BLACK);
    gfx_setCursor(10, 8);
    gfx_setTextColor(GFX_COLOR_WHITE, GFX_COLOR_BLACK);

    char result_buf[64];
    if (ok) {
        sprintf(result_buf, "Detected: %d  conf: %d%%", digit, confidence);
        gfx_puts(result_buf);
        draw_mnist_preview(226, 104);
        sprintf(result_buf, "Digit=%d Confidence=%d%%\r\n", digit, confidence);
        usart_send_string(result_buf);
    } else {
        gfx_puts((char *)"Could not detect digit");
        gfx_setCursor(10, 24);
        gfx_puts((char *)"Draw more or press Reset");
        usart_send_string("DIGIT_INFERENCE_FAILED\r\n");
    }

    draw_canvas_area();
    draw_controls();
    lcd_show_frame();
}

static void dense_layer(const float *input,
                        int input_count,
                        const int8_t *weights,
                        float weight_scale,
                        const float *bias,
                        float *output,
                        int output_count,
                        bool relu) {
    for (int row = 0; row < output_count; row++) {
        const int8_t *row_weights = weights + row * input_count;
        float acc = bias[row];

        for (int col = 0; col < input_count; col++) {
            acc += input[col] * ((float)row_weights[col] * weight_scale);
        }

        if (relu && acc < 0.0f) {
            acc = 0.0f;
        }

        output[row] = acc;
    }
}

static bool run_digit_inference(int *digit, int *confidence_percent) {
    if (!build_mnist_canvas()) {
        return false;
    }

    for (int i = 0; i < kMnistSize * kMnistSize; i++) {
        model_input[i] = mnist_canvas[i] / 255.0f;
    }

    dense_layer(model_input, MNIST_INPUT_SIZE,
                dense0_weights, dense0_weight_scale, dense0_bias,
                hidden0, MNIST_HIDDEN_SIZE, true);
    dense_layer(hidden0, MNIST_HIDDEN_SIZE,
                dense1_weights, dense1_weight_scale, dense1_bias,
                hidden1, MNIST_HIDDEN_SIZE, true);
    dense_layer(hidden1, MNIST_HIDDEN_SIZE,
                dense2_weights, dense2_weight_scale, dense2_bias,
                logits, MNIST_OUTPUT_SIZE, false);

    int best_digit = 0;
    float best_score = logits[0];
    for (int i = 1; i < 10; i++) {
        float score = logits[i];
        if (score > best_score) {
            best_score = score;
            best_digit = i;
        }
    }

    float softmax_sum = 0.0f;
    for (int i = 0; i < 10; i++) {
        softmax_sum += expf(logits[i] - best_score);
    }

    int confidence = 0;
    if (softmax_sum > 0.0f) {
        confidence = (int)((100.0f / softmax_sum) + 0.5f);
    }

    if (confidence < 0) confidence = 0;
    if (confidence > 100) confidence = 100;

    *digit = best_digit;
    *confidence_percent = confidence;
    return true;
}

int main(void) {
    // 1. Core Init
    clock_setup(); 
    usart_clock_setup();
    usart_setup();
    
    gpio_setup();
    
    // 2. Setup External Memory & Display
    sdram_init(); 
    lcd_spi_init(); 
    gfx_init(lcd_draw_pixel, GFX_WIDTH, GFX_HEIGHT);
    
    // 3. Initialize Touch Controller
    touch_init();

    usart_send_string("STMPE811 Touch Driver Initialized.\r\n");
    usart_send_string("MNIST digit model ready.\r\n");

    clear_drawing_state();
    show_prompt("Ready");
    usart_send_string("MNIST_READY\r\n");

    int touch_x = 0;
    int touch_y = 0;
    int last_touch_x = 0;
    int last_touch_y = 0;
    bool drawing = false;
    bool touch_was_down = false;

    while (1) {
        if (touch_read(&touch_x, &touch_y)) {
            if (!touch_was_down && point_in_predict_button(touch_x, touch_y)) {
                int digit = -1;
                int confidence = 0;
                bool ok = run_digit_inference(&digit, &confidence);
                show_prediction_result(ok, digit, confidence);
                drawing = false;
                touch_was_down = true;
                continue;
            }

            if (!touch_was_down && point_in_reset_button(touch_x, touch_y)) {
                clear_drawing_state();
                show_prompt("Ready");
                drawing = false;
                touch_was_down = true;
                continue;
            }

            if (point_in_button_area(touch_x, touch_y) || !point_in_canvas(touch_x, touch_y)) {
                drawing = false;
                touch_was_down = true;
                continue;
            }

            if (drawing && should_connect_points(last_touch_x, last_touch_y, touch_x, touch_y)) {
                draw_stroke_segment(last_touch_x, last_touch_y, touch_x, touch_y);
                draw_canvas_segment(last_touch_x, last_touch_y, touch_x, touch_y);
            } else {
                gfx_fillCircle(touch_x, touch_y, 2, GFX_COLOR_RED);
                mark_canvas_point(touch_x, touch_y);
            }

            drawing = true;
            last_touch_x = touch_x;
            last_touch_y = touch_y;
            
            // Push changes to screen
            // In a tight loop, this takes ~70ms, so it might throttle plotting.
            lcd_show_frame();

            // Log coordinates
            char dbg_buf[64];
            sprintf(dbg_buf, "Touch: X=%d Y=%d\r\n", touch_x, touch_y);
            usart_send_string(dbg_buf);
        } else {
            if (drawing) {
                drawing = false;
            }
            touch_was_down = false;
        }
    }
}
