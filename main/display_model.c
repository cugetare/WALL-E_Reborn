#include "display_model.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

bool walle_display_text_pixel(const char *text,unsigned x,unsigned y,unsigned left,unsigned top) {
    /* 5x7 column font, doubled; all diagnostics are ASCII and read left to right. */
    static const uint8_t font[36][5]={
      {0x3e,0x51,0x49,0x45,0x3e},{0,0x42,0x7f,0x40,0},{0x42,0x61,0x51,0x49,0x46},
      {0x21,0x41,0x45,0x4b,0x31},{0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
      {0x3c,0x4a,0x49,0x49,0x30},{1,0x71,9,5,3},{0x36,0x49,0x49,0x49,0x36},{6,0x49,0x49,0x29,0x1e},
      {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},
      {0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,9,9,9,1},
      {0x3e,0x41,0x49,0x49,0x7a},{0x7f,8,8,8,0x7f},{0,0x41,0x7f,0x41,0},
      {0x20,0x40,0x41,0x3f,1},{0x7f,8,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
      {0x7f,2,0x0c,2,0x7f},{0x7f,4,8,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},
      {0x7f,9,9,9,6},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,9,0x19,0x29,0x46},
      {0x46,0x49,0x49,0x49,0x31},{1,1,0x7f,1,1},{0x3f,0x40,0x40,0x40,0x3f},
      {0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,8,0x14,0x63},
      {7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43}};
    if(!text || x<left || y<top || y>=top+14) return false;
    unsigned index=(x-left)/12,column=((x-left)%12)/2,row=(y-top)/2;
    if(index>=strlen(text)||column>=5) return false;
    char c=text[index]; int n=c>='0'&&c<='9'?c-'0':c>='A'&&c<='Z'?c-'A'+10:-1;
    if(c=='.') return column==2&&row==6;
    if(c=='-') return row==3;
    return n>=0 && (font[n][column]&(1U<<row));
}

/* RGB565 values. The hardware renderer swaps them to the LCD's byte order. */
#define C_BLACK  0x0000U
#define C_WHITE  0xFFFFU
#define C_GREEN  0x07E0U
#define C_YELLOW 0xFFE0U
#define C_RED    0xF800U
#define C_BLUE   0x001FU
#define C_CYAN   0x07FFU
#define C_ORANGE 0xFD20U
#define C_NAVY   0x0008U
bool walle_display_battery_warning(float voltage, uint8_t battery_percent)
{
    return !isfinite(voltage) || voltage < 1.0f || battery_percent == 0U;
}

static uint16_t status_color(walle_display_status_t status)
{
    switch (status) {
        case WALLE_DISPLAY_CALIBRATION: return C_ORANGE;
        case WALLE_DISPLAY_READY: return C_GREEN;
        case WALLE_DISPLAY_OTA: return C_CYAN;
        case WALLE_DISPLAY_ERROR: return C_RED;
        case WALLE_DISPLAY_BOOT:
        default: return C_BLUE;
    }
}

static uint8_t glyph_row(char character, unsigned row)
{
    static const uint8_t s[7] = {0x0E, 0x11, 0x10, 0x0E, 0x01, 0x11, 0x0E};
    static const uint8_t o[7] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    static const uint8_t l[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
    static const uint8_t a[7] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    static const uint8_t r[7] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
    static const uint8_t c[7] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
    static const uint8_t h[7] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    static const uint8_t g[7] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E};
    static const uint8_t e[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
    static const uint8_t v[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04};
    if (row >= 7U) return 0;
    switch (character) {
        case 'S': return s[row]; case 'O': return o[row];
        case 'L': return l[row]; case 'A': return a[row];
        case 'R': return r[row]; case 'C': return c[row];
        case 'H': return h[row]; case 'G': return g[row];
        case 'E': return e[row]; case 'V': return v[row];
        default: return 0;
    }
}

static bool in_solar_label(unsigned x, unsigned y)
{
    static const char label[] = "SOLAR CHARGE LEVEL";
    const unsigned start_x = 12U;
    const unsigned start_y = 18U;
    const unsigned cell_width = 12U;
    if (x < start_x || x >= start_x + 18U * cell_width ||
        y < start_y || y >= start_y + 14U) return false;
    const unsigned index = (x - start_x) / cell_width;
    const unsigned cell_x = (x - start_x) % cell_width;
    const unsigned column = cell_x / 2U;
    const unsigned row = (y - start_y) / 2U;
    if (column >= 5U) return false;
    return (glyph_row(label[index], row) & (1U << (4U - column))) != 0;
}

static bool in_sun(unsigned x, unsigned y)
{
    const int dx = (int)x - 62;
    const int dy = (int)y - 108;
    const int radius_squared = dx * dx + dy * dy;
    if (radius_squared <= 18 * 18) return true;
    const bool horizontal_ray = dy >= -3 && dy <= 3 &&
        ((dx >= 25 && dx <= 40) || (dx <= -25 && dx >= -40));
    const bool vertical_ray = dx >= -3 && dx <= 3 &&
        ((dy >= 25 && dy <= 40) || (dy <= -25 && dy >= -40));
    const int diagonal_a = dx - dy;
    const int diagonal_b = dx + dy;
    const bool diagonal_ray =
        ((diagonal_a >= -3 && diagonal_a <= 3) ||
         (diagonal_b >= -3 && diagonal_b <= 3)) &&
        radius_squared >= 25 * 25 && radius_squared <= 40 * 40;
    return horizontal_ray || vertical_ray || diagonal_ray;
}

static uint16_t play_pixel(unsigned x, unsigned y, uint8_t battery_percent)
{
    if (in_solar_label(x, y)) return C_YELLOW;
    if (in_sun(x, y)) return C_YELLOW;

    /* Ten discrete cells echo the prop display rather than a modern UI bar. */
    const bool border = x >= 44U && x < 216U && y >= 206U && y < 252U;
    const bool inner = x >= 50U && x < 210U && y >= 214U && y < 244U;
    if (border && !inner) return C_YELLOW;
    if (inner) {
        const unsigned segment = (x - 50U) / 16U;
        const unsigned within_segment = (x - 50U) % 16U;
        const unsigned active_segments = (battery_percent + 9U) / 10U;
        if (within_segment < 12U && segment < active_segments) return C_YELLOW;
        return C_BLACK;
    }
    return C_NAVY;
}

uint16_t walle_display_pixel(unsigned x, unsigned y, uint8_t battery_percent,
                             bool drive_armed, walle_display_status_t status,
                             walle_display_mode_t mode)
{
    if (x >= WALLE_LCD_WIDTH || y >= WALLE_LCD_HEIGHT) return C_BLACK;
    if (battery_percent > 100U) battery_percent = 100U;

    /* Service and safety states override the cosmetic play screen. */
    if (mode == WALLE_DISPLAY_PLAY && status != WALLE_DISPLAY_OTA &&
        status != WALLE_DISPLAY_ERROR && status != WALLE_DISPLAY_CALIBRATION)
        return play_pixel(x, y, battery_percent);

    if (y < 28U) return status_color(status);

    /* Large battery glyph, readable through WALL-E's 24 x 33 mm window. */
    const bool body = x >= 28U && x < 204U && y >= 72U && y < 198U;
    const bool body_inner = x >= 38U && x < 194U && y >= 82U && y < 188U;
    const bool terminal = x >= 204U && x < 220U && y >= 112U && y < 158U;
    if (terminal) return C_WHITE;
    if (body && !body_inner) return C_WHITE;
    if (body_inner) {
        const unsigned filled = (156U * battery_percent) / 100U;
        if (x < 38U + filled) {
            if (battery_percent <= 15U) return C_RED;
            if (battery_percent <= 35U) return C_YELLOW;
            return C_GREEN;
        }
        return C_BLACK;
    }

    /* Bottom bar is an unambiguous motion-state indicator. */
    if (x >= 28U && x < 220U && y >= 232U && y < 252U)
        return drive_armed ? C_GREEN : C_RED;
    return C_NAVY;
}
