#include <tice.h>
#include <graphx.h>
#include <keypadc.h>
#include <string.h>
#include <fileioc.h>

// Palette IDs
#define EMPTY 0
#define WRONG 1
#define PARTIAL 2
#define CORRECT 3
#define TEXT 4
#define STATUS 5

void init_palette()
{
	gfx_palette[EMPTY] = gfx_RGBTo1555(0x12, 0x12, 0x13);
    gfx_palette[WRONG] = gfx_RGBTo1555(0x3a, 0x3a, 0x3c);
    gfx_palette[PARTIAL] = gfx_RGBTo1555(0xb5, 0x9f, 0x3b);
    gfx_palette[CORRECT] = gfx_RGBTo1555(0x53, 0x8d, 0x4e);
    gfx_palette[TEXT] = gfx_RGBTo1555(255, 255, 255);
    gfx_palette[STATUS] = gfx_RGBTo1555(0, 0, 0);
}

// Key-letter lookup table
const uint8_t KEYMAP[26] = 
{
	sk_Math, sk_Apps, sk_Prgm,
	sk_Recip, sk_Sin, sk_Cos, sk_Tan, sk_Power,
	sk_Square, sk_Comma, sk_LParen, sk_RParen, sk_Div,
	sk_Log, sk_7, sk_8, sk_9, sk_Mul,
	sk_Ln, sk_4, sk_5, sk_6, sk_Sub,
	sk_Store, sk_1, sk_2
};

// Game configuration
#define WORD_LENGTH 	5
#define GAME_TRIES 		6

// Graphics constants
#define CELL_SIZE 		25
#define CELL_SPACING 	1
#define GAME_WIDTH 		(WORD_LENGTH * CELL_SIZE + (WORD_LENGTH - 1) * CELL_SPACING)
#define GAME_HEIGHT 	(GAME_TRIES * CELL_SIZE + (GAME_TRIES - 1) * CELL_SPACING)
#define GAME_LEFT 		((320 / 2) - (GAME_WIDTH / 2))
#define GAME_TOP 		((240 / 2) - (GAME_HEIGHT / 2))
#define FONT_SIZE		24

// Returns the scancode of the pressed key (when it is first pushed down)
// 0 if no key is pressed
uint8_t get_single_key_pressed(void) 
{
    static uint8_t last_key;
    uint8_t only_key = 0;
    kb_Scan();
    for (uint8_t key = 1, group = 7; group; --group) 
    {
        for (uint8_t mask = 1; mask; mask <<= 1, ++key) 
        {
            if (kb_Data[group] & mask) 
            {
                if (only_key) 
                {
                    last_key = 0;
                    return 0;
                } 
                else 
                {
                    only_key = key;
                }
            }
        }
    }
    if (only_key == last_key) 
    {
        return 0;
    }
    last_key = only_key;
    return only_key;
}

char lose[]  = "The word was: XXXXX";
char* word = &lose[14]; // horrible C-string hack, saves a string allocation

typedef struct
{
	char value;
	uint8_t color;
} cell;

// Game data, saved on win or exit
struct
{
	uint16_t current_word;
	cell grid[GAME_TRIES][WORD_LENGTH];
	uint8_t current_line;
	uint8_t current_column;
	bool game_finished;
} game = {0};

// Letter occurrences counter
uint8_t letcount[26] = {0};

bool load_error = false;

// Loads the current word from the word list
void load_word()
{
	ti_var_t var = ti_Open("WORDLE", "r");
	if (!var)
	{
		load_error = true;
		return;
	}
	uint16_t list_size = ti_GetSize(var) / WORD_LENGTH;
	game.current_word %= list_size;
	ti_Seek(game.current_word * WORD_LENGTH, SEEK_CUR, var);
	ti_Read(word, WORD_LENGTH, 1, var);
	ti_Close(var);
	memset(letcount, 0, sizeof(letcount));
	for (uint8_t i = 0; i < WORD_LENGTH; i++)
		letcount[word[i] - 'A']++;
}

// Checks whether the submitted word exists in the word list
bool word_exists()
{
	ti_var_t var = ti_Open("WORDLE", "r");
	uint16_t list_size = ti_GetSize(var) / WORD_LENGTH;
	char* ptr = ti_GetDataPtr(var);
	bool result = false;
	while (list_size--)
	{
		cell* wptr = &game.grid[game.current_line][0];
		for (uint8_t i = 0; i < WORD_LENGTH; i++, wptr++)
		{
			if (wptr->value != ptr[i])
			{
				goto different;
			}
		}
		result = true;
		break;
	different:
		ptr += WORD_LENGTH;
		continue;
	}
	
	ti_Close(var);
	
	return result;
}

ti_var_t stats;

// Saves the game data in the WORDLE2 AppVar
void save_stats()
{
	ti_Rewind(stats);
	ti_Write(&game, sizeof(game), 1, stats);
}

// Draws the game grid
void draw_grid()
{
	gfx_SetTextFGColor(TEXT);
	gfx_SetTextScale(3, 3);
	
	cell* ptr = &game.grid[0][0];
	
	const uint24_t STRIDE = CELL_SIZE + CELL_SPACING;
	for (uint8_t y = 0; y < GAME_TRIES; y++)
	{
		for (uint8_t x = 0; x < WORD_LENGTH; x++, ptr++)
		{
			gfx_SetColor(ptr->color);
			uint24_t sx = GAME_LEFT + x * STRIDE;
			uint24_t sy = GAME_TOP + y * STRIDE;
			gfx_FillRectangle(sx, sy, CELL_SIZE, CELL_SIZE);
			if (ptr->value)
			{
				gfx_SetTextXY(sx + 2, sy + 2);
				gfx_PrintChar(ptr->value);
			}
		}
	}
}

bool invalid_word_warning = false;

// Processes the submitted word; checks whether it exists and computes the color result
void check_word()
{
	if (!word_exists())
	{
		invalid_word_warning = true;
	}
	else
	{
		cell* ptr = &game.grid[game.current_line][0];
		const char* wptr = &word[0];
		bool win = true;
		uint8_t inpcount[26] = {0};
		
		// first pass, matching letters
		for (uint8_t x = 0; x < WORD_LENGTH; x++, ptr++, wptr++)
		{
			if (ptr->value == *wptr)
			{
				uint8_t char_index = ptr->value - 'A';
				inpcount[char_index]++;
				ptr->color = CORRECT;
			}
		}
		
		// second pass, other letters
		ptr = &game.grid[game.current_line][0];
		wptr = &word[0];
		for (uint8_t x = 0; x < WORD_LENGTH; x++, ptr++, wptr++)
		{	
			if (ptr->value != *wptr)
			{
				uint8_t char_index = ptr->value - 'A';
				uint8_t count = inpcount[char_index]++;
				uint8_t real = letcount[char_index];
				win = false;
				if (count < real)
					ptr->color = PARTIAL;
				else
					ptr->color = WRONG;
			}
		}
		
		if (!win) // if the word doesn't match the target, go to the next line
		{
			game.current_line++;
			game.current_column = 0;
		}
		else // otherwise, save stats and start new game
		{
			game.current_word += 89; // not going to bother writing an RNG for this
			save_stats();
			load_word();
		}
		
		if (win || game.current_line == GAME_TRIES) // mark game as finished if the player has won or lost
		{
			game.game_finished = true;
		}
	}
}

void handle_key_input(uint8_t key)
{
	// check if the key corresponds to a letter
	for (uint8_t l = 0; l < 26; l++)
	{
		if (key == KEYMAP[l])
		{
			char letter = 'A' + l;
			game.grid[game.current_line][game.current_column].value = letter;
			game.current_column++;
			break;
		}
	}
}

void display_status_message()
{
	gfx_SetTextXY(20, 210);
	gfx_SetTextFGColor(STATUS);
	gfx_SetTextScale(2, 2);
	
	if (invalid_word_warning)
	{
		gfx_PrintString("Not in word list");
	}
	else if (game.game_finished)
	{
		if (game.current_line == GAME_TRIES)
		{
			gfx_PrintString(lose);
		}
		else
		{
			gfx_PrintString("Congratulations!");
		}
	}
}

int main(void)
{
	stats = ti_Open("WORDLE2", "r+");
	
	if (!stats) // create game stats file if it does not exist
	{
		stats = ti_Open("WORDLE2", "w");
		ti_Write(&game, sizeof(game), 1, stats);
		ti_Close(stats);
		stats = ti_Open("WORDLE2", "r+");
	}
	else
	{
		ti_Read(&game, sizeof(game), 1, stats);
	}

	load_word();
	
	if (load_error)
	{
		os_PutStrLine("Missing WORDLE AppVar.");
		while (!os_GetCSC());
		ti_Close(stats);
		return 1;
	}
	
    gfx_Begin();
    
    init_palette();

    gfx_SetDrawBuffer();

    while (1) 
    { 
		gfx_FillScreen(TEXT);
		
        draw_grid();
		
		display_status_message();
        
        gfx_SwapDraw();
        
        uint8_t key;
        while (!(key = get_single_key_pressed()));
        
        invalid_word_warning = false; // clear warning status
        
        if (key == sk_Clear)
		{
			break;
		}
		
		if (game.game_finished)
		{
			if (key == sk_Enter)
			{
				game.current_line = 0;
				game.current_column = 0;
				memset(game.grid, 0, sizeof(game.grid));
				game.game_finished = false;
			}
		}
		else
		{
			if (key == sk_Del)
			{
				if (game.current_column > 0)
				{
					game.grid[game.current_line][--game.current_column].value = 0;
				}
			}
			else if (key == sk_Enter)
			{
				if (game.current_column == WORD_LENGTH)
				{
					check_word();
				}
			}
			else
			{
				if (game.current_column < WORD_LENGTH)
				{
					handle_key_input(key);
				}
			}
		}
    }

	save_stats();
	ti_Close(stats);
    gfx_End();

    return 0;
}
