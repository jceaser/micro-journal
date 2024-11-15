#include "WordProcessor.h"
#include "../../display_EPD.h"

//
#include "app/app.h"
#include "editor/editor.h"
#include "keyboard/keyboard.h"
#include "display/display.h"
#include "keyboard/hid/nimble/ble.h"

//
int last_sleep = millis();
//
#define MARGIN 5
//
const int cols = 47;
const int rows = 8;
const int font_width = 20;
const int font_height = 49;
const int editY = font_height * (rows + 1);

// status bar
const int status_height = 30;
const int statusY = EPD_HEIGHT - status_height;

// clear screen
bool clear_request = true;
bool cleared = true;

int startLine = -1;

//
void WP_setup()
{
    app_log("Word Processor GUI Setup\n");

    // editor instantiate
    Editor &editor = Editor::getInstance();
    editor.screenBuffer.rows = rows;
    editor.screenBuffer.cols = cols;

    // load file from the editor
    JsonDocument &app = app_status();
    int file_index = app["config"]["file_index"].as<int>();

    //
    Editor::getInstance().loadFile(format("/%d.txt", file_index));

    //
    app_log("Word Processor Initialized %d.txt\n", file_index);

    // clear background
    clear_request = true;
}

//
void WP_render()
{
    // Turn on the display
    epd_poweron();

    // Clear Background
    if (clear_request)
    {
        // clear background
        epd_clear();
        clear_request = false;

        // mark that the screen is cleared
        // so that following render functions will redraw
        cleared = true;
    }

    //
    WP_check_saved();
    WP_check_sleep();

    // Bottom Status
    WP_render_status();

    // BLINK CURSOR
    WP_render_cursor();

    // RENDER TEXT
    WP_render_text();

    // Turn off the display
    epd_poweroff_all();

    // clear background flag off
    cleared = false;
}

void WP_render_text_line(int i, int cursorY, uint8_t *framebuffer)
{
    char *line = Editor::getInstance().screenBuffer.line_position[i];
    int length = Editor::getInstance().screenBuffer.line_length[i];
    int cursorX = MARGIN;
    // render
    if (line != NULL && length > 0)
    {
        char row[256];

        // Copy the line content to row, but not more than 255 characters
        int copyLength = (length < 255) ? length : 255;
        strncpy(row, line, copyLength);

        // Null-terminate the string
        row[copyLength] = '\0';
        writeln(display_EPD_font(), row, &cursorX, &cursorY, framebuffer);
    }
}

//
void WP_render_text()
{
    //
    JsonDocument &app = app_status();

    // Cursor Information
    static int cursorLine_prev = 0;
    static int cursorLinePos_prev = 0;
    int cursorLine = Editor::getInstance().fileBuffer.cursorLine;
    int cursorLinePos = Editor::getInstance().fileBuffer.cursorLinePos;

    //
    int totalLine = Editor::getInstance().screenBuffer.total_line;
    int rows = Editor::getInstance().screenBuffer.rows;

    // start editing from 2nd Line
    if (startLine == -1)
    {
        startLine = max(cursorLine - 2, 0);
        app_log("Start Line Init: %d -> ", startLine);
    }

    // when reaching the end of the screen reset the startLine
    if (totalLine - startLine > rows)
    {
        clear_request = true;
        startLine = max(cursorLine - 2, 0);
        app_log("Start Line Reset: %d -> ", startLine);
    }

    //
    // Middle part of the text will be rendered
    // Only when refresh background is called
    //
    if (cleared)
    {
        app_log("Text Render Cleared\n");

        //
        int cursorY = font_height;
        for (int i = startLine; i <= totalLine; i++)
        {
            // stop when exceeding row
            if (totalLine - i > rows)
                break;

            if (i >= 0)
                WP_render_text_line(i, cursorY, display_EPD_framebuffer());

            // increase the line
            cursorY += font_height;
        }

        //
        // render frame to the display
        epd_draw_grayscale_image(epd_full_screen(), display_EPD_framebuffer());
        memset(display_EPD_framebuffer(), 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
    }

    // Partial Refresh Logic
    if (cleared == false)
    {
        // new line
        if (cursorLine != cursorLine_prev)
        {
            // clear the currentLine and the previousLine
            app_log("clear the currentLine and the previousLine\n");
            Rect_t area =
                display_rect(
                    0,
                    font_height * (max(cursorLine - startLine - 1, 0)),
                    EPD_WIDTH,
                    font_height * 2);

            epd_clear_quick(area, 4, 50);

            // and redraw the line
            WP_render_text_line(cursorLine - 1, area.y + font_height, NULL);

            // render the entire line
            cursorLinePos_prev = 0;
        }

        // handle backspace
        if (cursorLinePos < cursorLinePos_prev && cursorLine == cursorLine_prev)
        {
            // clear the currentLine and the previousLine
            app_log("Handle Backspace\n");

            // delete a line and redraw the line
            Rect_t area =
                display_rect(
                    0,
                    font_height * (max(cursorLine - startLine, 0)),
                    EPD_WIDTH,
                    font_height * 2);

            epd_clear_quick(area, 4, 50);

            // and redraw the line
            cursorLinePos_prev = 0;
        }

        //
        // Bottom line will the the edit area
        // Draw the last character
        if (cursorLinePos != cursorLinePos_prev)
        {
            // render entire line
            int cursorX = MARGIN + font_width * cursorLinePos_prev;
            int cursorY = font_height * (cursorLine - startLine + 1);

            char *line = Editor::getInstance().screenBuffer.line_position[cursorLine];
            writeln(display_EPD_font(), line + cursorLinePos_prev, &cursorX, &cursorY, NULL);
        }
    }

    // reset prev flags
    cursorLinePos_prev = cursorLinePos;
    cursorLine_prev = cursorLine;
}

//
// Render Cursor
void WP_render_cursor()
{
    JsonDocument &app = app_status();

    // don't render at the round when screen is cleared
    if (cleared)
        return;

    // Cursor information
    static int renderedCursorX = -1;
    static int last = millis() + 2000;

    static int cursorLinePos_prev = 0;
    int cursorLinePos = Editor::getInstance().fileBuffer.cursorLinePos;
    int cursorLine = Editor::getInstance().fileBuffer.cursorLine;
    int cursorPos = Editor::getInstance().fileBuffer.cursorPos;

    // Calculate Cursor X position
    // reached the line where cursor is
    // distance X is cursorPos - pos
    int cursorX = 0;
    if (Editor::getInstance().fileBuffer.buffer[cursorPos - 1] != '\n' && cursorLinePos != 0)
    {
        // font width 12
        cursorX = MARGIN + cursorLinePos * font_width;
    }

    // Delete previous cursor line
    if (cursorLinePos != cursorLinePos_prev)
    {
        if (renderedCursorX > 0)
        {
            //
#ifdef DEBUG
            app_log("Delete previous cursor line\n");
#endif
            Rect_t area = display_rect(
                cursorLinePos_prev * font_width,
                font_height * (max(cursorLine - startLine, 0) + 1),
                font_width * abs(cursorLinePos - cursorLinePos_prev + 1),
                10);

            epd_clear_quick(area, 8, 50);

            // render the cursor
            renderedCursorX = -1;
        }

        // reset the timer
        last = millis();

        //
        cursorLinePos_prev = cursorLinePos;
    }

    // when there are no types for 5 seconds then
    // display the cursor
    if (renderedCursorX == -1 && last + 300 < millis())
    {
        // Cursor will be always at the bottom of the screen
        int cursorY = font_height * (max(cursorLine - startLine, 0) + 1);
        writeln(display_EPD_font(), "_", &cursorX, &cursorY, NULL);

        //
        renderedCursorX = cursorX;
    }
}

// Check if text is saved
void WP_check_saved()
{
    //
    static unsigned int last = millis();
    static int lastBufferSize = Editor::getInstance().fileBuffer.getBufferSize();
    int bufferSize = Editor::getInstance().fileBuffer.getBufferSize();

    //
    // when the file is saved then extend the autosave timer
    if (lastBufferSize != bufferSize)
    {
        last = millis();

        //
        lastBufferSize = bufferSize;
    }

    //
    // when idle for 4 seconds then auto save
    if (millis() - last > 4000)
    {
        //
        last = millis();

        if (!Editor::getInstance().saved)
        {
            Editor::getInstance().saveFile();
            clear_request = true;
        }
    }
}

void WP_check_sleep()
{
    // 600 seconds - 10 minutes
    if (millis() - last_sleep > 10 * 60 * 1000)
    {
        // if no action for 10 minute go to sleep
        last_sleep = millis();

        //
        JsonDocument &app = app_status();
        app["screen"] = SLEEPSCREEN;
    }
}

// display file number
// display character total
// display keyboard layout - Rev.5 and Rev.7
// display save status
#define STATUS_REFRESH 300
void WP_render_status()
{
    //
    JsonDocument &app = app_status();

    // status start Y position
    int cursorY = statusY + status_height - 8;
    static size_t num_prev = 0;

    // Draw non-refreshing section
    if (cleared)
    {
        // reset previous character size
        num_prev = 0;

        // FILE INDEX
        int cursorX = 16;
        String file = format("FILE %d", app["config"]["file_index"].as<int>());
        writeln((GFXfont *)&systemFont, file.c_str(), &cursorX, &cursorY, display_EPD_framebuffer());

        // KEYBOARD LAYOUT
        String layout = app["config"]["keyboard_layout"].as<String>();
        if (layout == "null" || layout.isEmpty())
            layout = "US"; // defaults to US layout

        cursorX = EPD_WIDTH - 120;
        writeln((GFXfont *)&systemFont, layout.c_str(), &cursorX, &cursorY, display_EPD_framebuffer());

        // BLUETOOTH STATUS
        if (keyboard_ble_connected())
        {
            epd_fill_circle(EPD_WIDTH - 80, cursorY - 8, 8, 0, display_EPD_framebuffer());
        }
    }

    // Draw periodically refreshing section
    static int last = millis();

    // when cleared redraw the size
    if (cleared)
        num_prev = -1;

    // FILE SIZE
    static bool num_changed = false;
    size_t num = Editor::getInstance().fileBuffer.seekPos + Editor::getInstance().fileBuffer.getBufferSize();
    if (num != num_prev)
    {
        //
        num_prev = num;

        //
        num_changed = true;

        // debounce for status_refresh amount
        last = millis();
    }

    if ((num_changed && last + STATUS_REFRESH < millis()) || cleared)
    {
        //
        last = millis();
        num_changed = false;

        //
        int cursorX = 160;

        //
        if (!cleared)
        {
            Rect_t area = display_rect(
                cursorX,
                statusY,
                450,
                status_height);
            //
            epd_clear_quick(area, 2, 100);
        }

        String formattedNumber = "";
        int digitCount = 0;
        if (num < 0)
        {
            formattedNumber += "-";
            num = -num;
        }
        do
        {
            if (digitCount > 0 && digitCount % 3 == 0)
            {
                formattedNumber = "," + formattedNumber;
            }
            formattedNumber = String(num % 10) + formattedNumber;
            num /= 10;
            digitCount++;
        } while (num > 0);

        formattedNumber += " characters";

        // SAVED STATUS
        if (Editor::getInstance().saved)
        {
            formattedNumber += " saved";
        }

        writeln((GFXfont *)&systemFont, formattedNumber.c_str(), &cursorX, &cursorY, NULL);
    }
}

//
void WP_keyboard(char key)
{
    // Every key stroke resets sleep timer
    last_sleep = millis();

    //
    JsonDocument &app = app_status();

    // Check if menu key is pressed
    if (key == MENU)
    {
        // Save before transitioning to the menu
        Editor::getInstance().saveFile();

        //
        app["screen"] = MENUSCREEN;
#ifdef DEBUG
        app_log("WP - Moving to Menu Screen\n");
#endif
    }

    else
    {
        // send the keys to the editor
        Editor::getInstance().keyboard(key);
    }
}
