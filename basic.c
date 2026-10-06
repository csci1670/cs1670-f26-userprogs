// TinyBASIC.cpp : An implementation of TinyBASIC in C
//
// Author : Mike Field - hamster@snap.net.nz
//
// Based on TinyBasic for 68000, by Gordon Brandly
// (see http://members.shaw.ca/gbrandly/68ktinyb.html)
//
// which itself was Derived from Palo Alto Tiny BASIC as
// published in the May 1976 issue of Dr. Dobb's Journal.
//
// 0.03 21/01/2011 : Added INPUT routine
//                 : Reorganised memory layout
//                 : Expanded all error messages
//                 : Break key added
//                 : Removed the calls to printf (left by debugging)


// Provided for CS 1670 Project 3. Everything this interpreter needs from your
// OS is in the WIRING section just below: four small functions, for starting
// up, writing a character, reading a character, and checking whether a key
// is waiting. Nothing else in the file touches your kernel.

#include "u_common.h"

// ---------------------------------------------------------------------------
// WIRING: the four places this program talks to your OS.
//
// As shipped, BASIC lives on the serial console (Quest 2): it reads with
// getc(), checks for Ctrl-C with hasc(), and writes with printf().
//
// In Quest 4 it moves onto the screen, into a window of its own. Only
// basic_start() and basic_putc() change: get your canvas from the kernel,
// set up the text-console code you wrote for your terminal in Quest 3 on it,
// and send every character through that instead of printf(). Input does not
// change at all -- your kernel already delivers keystrokes to whichever
// process has the focus.
// ---------------------------------------------------------------------------

// Called once, before anything else.
static void basic_start(void) {
  printf("\r\nTinyBASIC. Type a program, then RUN. Ctrl-C stops it.\r\n");
}

// Write one character.
//
// BASIC ends its lines with a bare '\n'. A serial terminal needs "\r\n" to
// get back to the left margin as well as down a line, so we add the '\r'.
static void basic_putc(char c) {
  if (c == '\n') {
    printf("\r\n");
  } else {
    printf("%c", c);
  }
}

// Read one character, waiting if there is none yet.
static char basic_getc(void) {
  return getc();
}

// Is there a character waiting? (Used to notice Ctrl-C while a program runs,
// without stopping to wait for a key.)
static int basic_key_waiting(void) {
  return hasc() != 0;
}

// ---------------------------------------------------------------------------

/* byte pinDef[] = {2, 3, 4, 5, 6, 13}; */
/* byte adcDef[] = {A0, A1, A2, A3, A4, A5}; */
/* byte pinDir[7];					//0=input 1=output 9=servo */
/* byte pinStates[7];					//0 or 1. */

// ASCII Characters
#define CR	'\r'
#define NL	'\n'
#define TAB	'\t'
#define BELL	'\b'
#define DEL	'\177'
#define SPACE   ' '
#define CTRLC	0x03
#define CTRLH	0x08
#define CTRLS	0x13
#define CTRLX	0x18

typedef short unsigned LINENUM;


/***********************************************************/
// Keyword table and constants - the last character has 0x80 added to it
static unsigned char keywords[] = {
	'L','I','S','T'+0x80,
	'L','O','A','D'+0x80,
	'N','E','W'+0x80,
	'R','U','N'+0x80,
	'S','A','V','E'+0x80,
	'N','E','X','T'+0x80,
	'L','E','T'+0x80,
	'I','F'+0x80,
	'G','O','T','O'+0x80,
	'G','O','S','U','B'+0x80,
	'R','E','T','U','R','N'+0x80,
	'R','E','M'+0x80,
	'F','O','R'+0x80,
	'I','N','P','U','T'+0x80,
	'P','R','I','N','T'+0x80,
	'P','O','K','E'+0x80,
	'S','T','O','P'+0x80,
	'B','Y','E'+0x80,
	'P','I','N','O','U','T'+0x80,
	'D','E','L','A','Y'+0x80,
	'S','E','R','V','O'+0x80,
	'P','I','N','M','O','D','E'+0x80,
	'P','W','M'+0x80,
	0
};

#define KW_LIST		0
#define KW_LOAD		1
#define KW_NEW		2
#define KW_RUN		3
#define KW_SAVE		4
#define KW_NEXT		5
#define KW_LET		6
#define KW_IF		7
#define KW_GOTO		8
#define KW_GOSUB	9
#define KW_RETURN	10
#define KW_REM		11
#define KW_FOR		12
#define KW_INPUT	13
#define KW_PRINT	14
#define KW_POKE		15
#define KW_STOP		16
#define KW_BYE		17
#define KW_PINOUT	18
#define KW_DELAY	19
#define KW_SERVO	20
#define KW_PINMODE	21
#define KW_PWM		22
#define KW_DEFAULT	23


struct stack_for_frame {
	char frame_type;
	char for_var;
	short int terminal;
	short int step;
	unsigned char *current_line;
	unsigned char *txtpos;
};

struct stack_gosub_frame {
	char frame_type;
	unsigned char *current_line;
	unsigned char *txtpos;
};

static unsigned char func_tab[] = {
	'P','E','E','K'+0x80,
	'P','I','N','I','N'+0x80,

	0
};
#define FUNC_PEEK    0
#define FUNC_PININ	 1
#define FUNC_UNKNOWN 2

static unsigned char to_tab[] = {
	'T','O'+0x80,
	0
};

static unsigned char step_tab[] = {
	'S','T','E','P'+0x80,
	0
};

static unsigned char relop_tab[] = {
	'>','='+0x80,
	'<','>'+0x80,
	'>'+0x80,
	'='+0x80,
	'<','='+0x80,
	'<'+0x80,
	0
};

#define RELOP_GE		0
#define RELOP_NE		1
#define RELOP_GT		2
#define RELOP_EQ		3
#define RELOP_LE		4
#define RELOP_LT		5
#define RELOP_UNKNOWN	6

#define VAR_SIZE sizeof(short int) // Size of variables in bytes

static unsigned char memory[1200];
static unsigned char *txtpos,*list_line;
static unsigned char expression_error;
static unsigned char *tempsp;
static unsigned char *stack_limit;
static unsigned char *program_start;
static unsigned char *program_end;
[[maybe_unused]] static unsigned char *stack; // Software stack for things that should go on the CPU stack
static unsigned char *variables_table;
static unsigned char *current_line;
static unsigned char *sp;
#define STACK_GOSUB_FLAG 'G'
#define STACK_FOR_FLAG 'F'
static unsigned char table_index;
static LINENUM linenum;

[[maybe_unused]] static const unsigned char okmsg[]		= "OK";
static const unsigned char badlinemsg[]		= "Invalid line number";
static const unsigned char invalidexprmsg[] = "Invalid expression";
static const unsigned char syntaxmsg[] = "Syntax Error";
static const unsigned char badinputmsg[] = "\nBad number";
static const unsigned char nomemmsg[]	= "Not enough memory!";
[[maybe_unused]] static const unsigned char initmsg[]	= "TinyBasic";
static const unsigned char memorymsg[]	= " Free.";
static const unsigned char breakmsg[]	= "[BREAK]";
static const unsigned char stackstuffedmsg[] = "Stack is stuffed!\n";
static const unsigned char unimplimentedmsg[]	= "Unimplemented";
static const unsigned char backspacemsg[]		= "\b \b";

static int inchar(void);
static void outchar(unsigned char c);
static void line_terminator(void);
static short int expression(void);
static unsigned char breakcheck(void);

static_assert(sizeof(short int) == 2);

static inline unsigned short read_uint16(unsigned short* p) {
    unsigned short v;
    char* pc = (char*)p;

    ((char*)&v)[0] = pc[0];
    ((char*)&v)[1] = pc[1];

    return v;
}

static inline unsigned short write_uint16(unsigned short* p, unsigned short v) {
    char* pc = (char*)p;
    char* pv = (char*)&v;

    pc[0] = pv[0];
    pc[1] = pv[1];

    return v;
}

static inline short read_int16(short* p) {
    return (short)read_uint16((unsigned short*)p);
}

static inline short write_int16(short* p, short v) {
    return (short)write_uint16((unsigned short*)p, (unsigned short)v);
}


static void delay(int cycles) {
    while (cycles > 0) {
	for(int i = 0; i < 10000; i++) {
	    __asm__ inline("nop");
	}
	cycles--;
    }
}

/***************************************************************************/
static void ignore_blanks(void)
{
	while(*txtpos == SPACE || *txtpos == TAB)
		txtpos++;
}

/***************************************************************************/
static void scantable(unsigned char *table)
{
	int i = 0;
	ignore_blanks();
	table_index = 0;
	while(1)
	{
		// Run out of table entries?
		if(table[0] == 0)
	    return;

		// Do we match this character?
		if(txtpos[i] == table[0])
		{
			i++;
			table++;
		}
		else
		{
			// do we match the last character of keywork (with 0x80 added)? If so, return
			if(txtpos[i]+0x80 == table[0])
			{
				txtpos += i+1;  // Advance the pointer to following the keyword
				ignore_blanks();
				return;
			}

			// Forward to the end of this keyword
			while((table[0] & 0x80) == 0)
				table++;

			// Now move on to the first character of the next word, and reset the position index
			table++;
			table_index++;
			i = 0;
		}
	}
}

/***************************************************************************/
static void pushb(unsigned char b)
{
	sp--;
	*sp = b;
}

/***************************************************************************/
static unsigned char popb()
{
	unsigned char b;
	b = *sp;
	sp++;
	return b;
}

/***************************************************************************/
static void printnum(int num)
{
	int digits = 0;

	if(num < 0)
	{
		num = -num;
		outchar('-');
	}

	do {
		pushb(num%10+'0');
		num = num/10;
		digits++;
	}
	while (num > 0);

	while(digits > 0)
	{
		outchar(popb());
		digits--;
	}
}
/***************************************************************************/
static unsigned short testnum(void)
{
	unsigned short num = 0;
	ignore_blanks();

	while(*txtpos>= '0' && *txtpos <= '9' )
	{
		// Trap overflows
		if(num >= 0xFFFF/10)
		{
			num = 0xFFFF;
			break;
		}

		num = num *10 + *txtpos - '0';
		txtpos++;
	}
	return	num;
}

/***************************************************************************/
unsigned char check_statement_end(void)
{
	ignore_blanks();
	return (*txtpos == NL) || (*txtpos == ':');
}

/***************************************************************************/
static void printmsgNoNL(const unsigned char *msg)
{
	while(*msg)
	{
		outchar(*msg);
		msg++;
	}
}

/***************************************************************************/
static unsigned char print_quoted_string(void)
{
	int i=0;
	unsigned char delim = *txtpos;
	if(delim != '"' && delim != '\'')
		return 0;
	txtpos++;

	// Check we have a closing delimiter
	while(txtpos[i] != delim)
	{
		if(txtpos[i] == NL)
			return 0;
		i++;
	}

	// Print the characters
	while(*txtpos != delim)
	{
		outchar(*txtpos);
		txtpos++;
	}
	txtpos++; // Skip over the last delimiter
	ignore_blanks();

	return 1;
}

/***************************************************************************/
static void printmsg(const unsigned char *msg)
{
    printmsgNoNL(msg);
    line_terminator();
}

/***************************************************************************/
unsigned char getln(char prompt)
{
	outchar(prompt);
	txtpos = program_end+sizeof(LINENUM);

	while(1)
	{
		char c = inchar();
		switch(c)
		{
			case CR:
			case NL:
		line_terminator();
				// Terminate all strings with a NL
				txtpos[0] = NL;
				return 1;
			case CTRLC:
				return 0;
			case CTRLH:
			case DEL:  // most terminals send DEL, not BS, for backspace
				if(txtpos == program_end)
					break;
				txtpos--;
				printmsgNoNL(backspacemsg);
				break;
			default:
				// We need to leave at least one space to allow us to shuffle the line into order
				if(txtpos == sp-2)
					outchar(BELL);
				else
				{
					txtpos[0] = c;
					txtpos++;
					outchar(c);
				}
		}
	}
}

/***************************************************************************/
static unsigned char *findline(void)
{
	unsigned char *line = program_start;
	while(1)
	{
	        if(line == program_end) {
			return line;
     	        }

		//if(((LINENUM *)line)[0] >= linenum)
		if(read_uint16(&((LINENUM*)line)[0]) >= linenum) {
			return line;
		}

		// Add the line length onto the current address, to get to the next line;
		line += line[sizeof(LINENUM)];
	}
}

/***************************************************************************/
static void toUppercaseBuffer(void)
{
	unsigned char *c = program_end+sizeof(LINENUM);
	unsigned char quote = 0;

	while(*c != NL)
	{
		// Are we in a quoted string?
		if(*c == quote)
			quote = 0;
		else if(*c == '"' || *c == '\'')
			quote = *c;
		else if(quote == 0 && *c >= 'a' && *c <= 'z')
			*c = *c + 'A' - 'a';
		c++;
	}
}

/***************************************************************************/
void printline()
{
	LINENUM line_num;

	//line_num = *((LINENUM *)(list_line));
	line_num = read_uint16((LINENUM*)list_line);
    list_line += sizeof(LINENUM) + sizeof(char);

	// Output the line */
	printnum(line_num);
	outchar(' ');
	while(*list_line != NL)
    {
		outchar(*list_line);
		list_line++;
	}
	list_line++;
	line_terminator();
}

/***************************************************************************/
static short int expr4(void)
{
	short int a = 0;

	if(*txtpos == '0')
	{
		txtpos++;
		a = 0;
		goto success;
	}

	if(*txtpos >= '1' && *txtpos <= '9')
	{
		do	{
			a = a*10 + *txtpos - '0';
			txtpos++;
		} while(*txtpos >= '0' && *txtpos <= '9');
			goto success;
	}

	// Is it a function or variable reference?
	if(txtpos[0] >= 'A' && txtpos[0] <= 'Z')
	{
		// Is it a variable reference (single alpha)
		if(txtpos[1] < 'A' || txtpos[1] > 'Z')
		{
		        //a = ((short int *)variables_table)[*txtpos - 'A'];
		    a = read_int16((&((short int*)variables_table)[*txtpos - 'A']));
		        txtpos++;
			goto success;
		}

		// Is it a function with a single parameter
		scantable(func_tab);
		if(table_index == FUNC_UNKNOWN)
			goto expr4_error;

		unsigned char f = table_index;

		if(*txtpos != '(')
			goto expr4_error;

		txtpos++;
		a = expression();
		if(*txtpos != ')')
				goto expr4_error;
		txtpos++;
		switch(f)
		{
		        case FUNC_PEEK: {
			        char* p = (char*)((uintptr_t)a);
				a = *p;
			        //a =  EEPROM.read(a);
				//printmsg(unimplimentedmsg);
				goto success;
				//goto expr4_error;
			}
			case FUNC_PININ:
				/* if(a > -1 and a < 6 and pinDir[a] == 0) {	//Within pin range and pin is set to input? */
				/*	a = digitalRead(pinDef[a]); */
				/* } */
				/* goto success; */
				printmsg(unimplimentedmsg);
				goto expr4_error;
		}
	}

	if(*txtpos == '(')
	{
		txtpos++;
		a = expression();
		if(*txtpos != ')')
			goto expr4_error;

		txtpos++;
		goto success;
	}

expr4_error:
	expression_error = 1;
success:
	ignore_blanks();
	return a;
}

/***************************************************************************/
static short int expr3(void)
{
	short int a,b;

	a = expr4();
	while(1)
	{
		if(*txtpos == '*')
		{
			txtpos++;
			b = expr4();
			a *= b;
		}
		else if(*txtpos == '/')
		{
			txtpos++;
			b = expr4();
			if(b != 0)
				a /= b;
			else
				expression_error = 1;
		}
		else
			return a;
	}
}

/***************************************************************************/
static short int expr2(void)
{
	short int a,b;

	if(*txtpos == '-' || *txtpos == '+')
		a = 0;
	else
		a = expr3();

	while(1)
	{
		if(*txtpos == '-')
		{
			txtpos++;
			b = expr3();
			a -= b;
		}
		else if(*txtpos == '+')
		{
			txtpos++;
			b = expr3();
			a += b;
		}
		else
			return a;
	}
}
/***************************************************************************/
static short int expression(void)
{
	short int a,b;

	a = expr2();
	// Check if we have an error
	if(expression_error)	return a;

	scantable(relop_tab);
	if(table_index == RELOP_UNKNOWN)
		return a;

	switch(table_index)
	{
	case RELOP_GE:
		b = expr2();
		if(a >= b) return 1;
		break;
	case RELOP_NE:
		b = expr2();
		if(a != b) return 1;
		break;
	case RELOP_GT:
		b = expr2();
		if(a > b) return 1;
		break;
	case RELOP_EQ:
		b = expr2();
		if(a == b) return 1;
		break;
	case RELOP_LE:
		b = expr2();
		if(a <= b) return 1;
		break;
	case RELOP_LT:
		b = expr2();
		if(a < b) return 1;
		break;
	}
	return 0;
}

/***************************************************************************/
void loop()
{
	unsigned char *start;
	unsigned char *newEnd;
	unsigned char linelen;

	variables_table = memory;
	program_start = memory + 27*VAR_SIZE;
	program_end = program_start;
	sp = memory+sizeof(memory);  // Needed for printnum
	//printmsg(initmsg);
	printnum(sp-program_end);
	printmsg(memorymsg);
	//printf("memory %p, start %p, end %p, sp %p\n", memory, program_start, program_end, sp);

warmstart:
	// this signifies that it is running in 'direct' mode.
	current_line = 0;
	sp = memory+sizeof(memory);
	//printmsg(okmsg);

prompt:
	while(!getln('>'))
		line_terminator();
	toUppercaseBuffer();

	txtpos = program_end+sizeof(unsigned short);

	// Find the end of the freshly entered line
	while(*txtpos != NL)
		txtpos++;

	// Move it to the end of program_memory
	{
		unsigned char *dest;
		dest = sp-1;
		while(1)
		{
			*dest = *txtpos;
			if(txtpos == program_end+sizeof(unsigned short))
				break;
			dest--;
			txtpos--;
		}
		txtpos = dest;
	}

	// Now see if we have a line number
	linenum = testnum();
	ignore_blanks();
	if(linenum == 0)
		goto direct;

	if(linenum == 0xFFFF)
		goto badline;
	// Find the length of what is left, including the (yet-to-be-populated) line header
	linelen = 0;
	while(txtpos[linelen] != NL)
		linelen++;
	linelen++; // Include the NL in the line length
	linelen += sizeof(unsigned short)+sizeof(char); // Add space for the line number and line length
	// Now we have the number, add the line header.
	txtpos -= 3;
	//*((unsigned short *)txtpos) = linenum;
	write_uint16((unsigned short *)txtpos, linenum);
	txtpos[sizeof(LINENUM)] = linelen;

	// Merge it into the rest of the program
	start = findline();
	// If a line with that number exists, then remove it
	//if(start != program_end && *((LINENUM *)start) == linenum)
	if(start != program_end && read_uint16((LINENUM*)start) == linenum)
	{
		unsigned char *dest, *from;
		unsigned tomove;

		from = start + start[sizeof(LINENUM)];
		dest = start;

		tomove = program_end - from;
		while( tomove > 0)
		{
			*dest = *from;
			from++;
			dest++;
			tomove--;
		}
		program_end = dest;
	}

	if(txtpos[sizeof(LINENUM)+sizeof(char)] == NL) // If the line has no txt, it was just a delete
		goto prompt;



	// Make room for the new line, either all in one hit or lots of little shuffles
	while(linelen > 0)
	{
		unsigned int tomove;
		unsigned char *from,*dest;
		unsigned int space_to_make;

		space_to_make = txtpos - program_end;

		if(space_to_make > linelen)
			space_to_make = linelen;
		newEnd = program_end+space_to_make;
		tomove = program_end - start;


		// Source and destination - as these areas may overlap we need to move bottom up
		from = program_end;
		dest = newEnd;
		while(tomove > 0)
		{
			from--;
			dest--;
			*dest = *from;
			tomove--;
		}

		// Copy over the bytes into the new space
		for(tomove = 0; tomove < space_to_make; tomove++)
		{
			*start = *txtpos;
			txtpos++;
			start++;
			linelen--;
		}
		program_end = newEnd;
	}
	goto prompt;

unimplemented:
	printmsg(unimplimentedmsg);
	goto prompt;

badline:
	printmsg(badlinemsg);
	goto prompt;
invalidexpr:
	printmsg(invalidexprmsg);
	goto prompt;
syntaxerror:
	printmsg(syntaxmsg);
	if(current_line != (void *)0)
	{
	   unsigned char tmp = *txtpos;
	   if(*txtpos != NL) {
	       *txtpos = '^';
	   }
	   list_line = current_line;
	   printline();
	   *txtpos = tmp;
	}
    line_terminator();
	goto prompt;

stackstuffed:
	printmsg(stackstuffedmsg);
	goto warmstart;
nomem:
	printmsg(nomemmsg);
	goto warmstart;

run_next_statement:
	while(*txtpos == ':')
		txtpos++;
	ignore_blanks();
	if(*txtpos == NL)
		goto execnextline;
	goto interperateAtTxtpos;

direct:
	txtpos = program_end+sizeof(LINENUM);
	if(*txtpos == NL)
		goto prompt;

interperateAtTxtpos:
	if(breakcheck())
	{
	  printmsg(breakmsg);
	  goto warmstart;
	}

	scantable(keywords);
	ignore_blanks();

	switch(table_index)
	{
		case KW_LIST:
			goto list;
		case KW_LOAD:
			goto unimplemented; /////////////////
		case KW_NEW:
			if(txtpos[0] != NL)
				goto syntaxerror;
			program_end = program_start;
			goto prompt;
		case KW_RUN:
			current_line = program_start;
			goto execline;
		case KW_SAVE:
			goto unimplemented; //////////////////////
		case KW_NEXT:
			goto next;
		case KW_LET:
			goto assignment;
		case KW_IF:
			{
			short int val;
			expression_error = 0;
			val = expression();
			if(expression_error || *txtpos == NL)
				goto invalidexpr;
			if(val != 0)
				goto interperateAtTxtpos;
			goto execnextline;
			}
		case KW_GOTO:
			expression_error = 0;
			linenum = expression();
			if(expression_error || *txtpos != NL)
				goto invalidexpr;
			current_line = findline();
			goto execline;

		case KW_GOSUB:
			goto gosub;
		case KW_RETURN:
			goto gosub_return;
		case KW_REM:
			goto execnextline;	// Ignore line completely
		case KW_FOR:
			goto forloop;
		case KW_INPUT:
			goto input;
		case KW_PRINT:
			goto print;
		case KW_POKE:
			goto poke;
		case KW_STOP:
			// This is the easy way to end - set the current line to the end of program attempt to run it
			if(txtpos[0] != NL)
				goto syntaxerror;
			current_line = program_end;
			goto execline;
		case KW_BYE:
			// Leave the basic interperater
			return;
		case KW_PINOUT:
			goto pinout;
		case KW_DELAY:
			goto delayT;
		case KW_SERVO:
			goto servo;
		case KW_PWM:
			goto doPWM;
		case KW_DEFAULT:
			goto assignment;
		default:
			break;
	}

execnextline:
	if(current_line == (void *)0)		// Processing direct commands?
		goto prompt;
	current_line +=	 current_line[sizeof(LINENUM)];

execline:
	if(current_line == program_end) // Out of lines to run
		goto warmstart;
	txtpos = current_line+sizeof(LINENUM)+sizeof(char);
	goto interperateAtTxtpos;

input:
	{
		unsigned char isneg=0;
		unsigned char *temptxtpos;
		short int *var;
		ignore_blanks();
		if(*txtpos < 'A' || *txtpos > 'Z')
			goto syntaxerror;
		var = ((short int *)variables_table)+*txtpos-'A';
		txtpos++;
		if(!check_statement_end())
			goto syntaxerror;
again:
		temptxtpos = txtpos;
		(void)temptxtpos;
		if(!getln('?'))
			goto warmstart;

		// Go to where the buffer is read
		txtpos = program_end+sizeof(LINENUM);
		if(*txtpos == '-')
		{
			isneg = 1;
			txtpos++;
		}

		//*var = 0;
		write_int16(var, 0);
		do	{
			//*var = *var*10 + *txtpos - '0';
		        write_int16(var, read_int16(var)*10 + *txtpos - '0');
			txtpos++;
		} while(*txtpos >= '0' && *txtpos <= '9');
		ignore_blanks();
		if(*txtpos != NL)
		{
			printmsg(badinputmsg);
			goto again;
		}

		if(isneg) {
			//*var = -*var;
		        write_int16(var, -read_int16(var));
		}


		goto run_next_statement;
	}
forloop:
	{
		unsigned char var;
		short int initial, step, terminal;

		if(*txtpos < 'A' || *txtpos > 'Z')
			goto syntaxerror;
		var = *txtpos;
		txtpos++;

		scantable(relop_tab);
		if(table_index != RELOP_EQ)
			goto syntaxerror;

		expression_error = 0;
		initial = expression();
		if(expression_error)
			goto invalidexpr;

		scantable(to_tab);
		if(table_index != 0)
			goto syntaxerror;

		terminal = expression();
		if(expression_error)
			goto invalidexpr;

		scantable(step_tab);
		if(table_index == 0)
		{
			step = expression();
			if(expression_error)
				goto invalidexpr;
		}
		else
			step = 1;
		if(!check_statement_end())
			goto syntaxerror;


		if(!expression_error && *txtpos == NL)
		{
			struct stack_for_frame *f;
			if(sp + sizeof(struct stack_for_frame) < stack_limit)
				goto nomem;

			sp -= sizeof(struct stack_for_frame);
			f = (struct stack_for_frame *)sp;
			//((short int *)variables_table)[var-'A'] = initial; // XXX
			write_int16(&(((short int *)variables_table)[var-'A']), initial);
			f->frame_type = STACK_FOR_FLAG;
			f->for_var = var;
			f->terminal = terminal;
			f->step     = step;
			f->txtpos   = txtpos;
			f->current_line = current_line;
			goto run_next_statement;
		}
	}
	goto syntaxerror;

gosub:
	expression_error = 0;
	linenum = expression();
	if(expression_error)
		goto invalidexpr;
	if(!expression_error && *txtpos == NL)
	{
		struct stack_gosub_frame *f;
		if(sp + sizeof(struct stack_gosub_frame) < stack_limit)
			goto nomem;

		sp -= sizeof(struct stack_gosub_frame);
		f = (struct stack_gosub_frame *)sp;
		f->frame_type = STACK_GOSUB_FLAG;
		f->txtpos = txtpos;
		f->current_line = current_line;
		current_line = findline();
		goto execline;
	}
	goto syntaxerror;

next:
	// Fnd the variable name
	ignore_blanks();
	if(*txtpos < 'A' || *txtpos > 'Z')
		goto syntaxerror;
	txtpos++;
	if(!check_statement_end())
		goto syntaxerror;

gosub_return:
	// Now walk up the stack frames and find the frame we want, if present
	tempsp = sp;
	while(tempsp < memory+sizeof(memory)-1)
	{
		switch(tempsp[0])
		{
			case STACK_GOSUB_FLAG:
				if(table_index == KW_RETURN)
				{
					struct stack_gosub_frame *f = (struct stack_gosub_frame *)tempsp;
					current_line	= f->current_line;
					txtpos			= f->txtpos;
					sp += sizeof(struct stack_gosub_frame);
					goto run_next_statement;
				}
				// This is not the loop you are looking for... so Walk back up the stack
				tempsp += sizeof(struct stack_gosub_frame);
				break;
			case STACK_FOR_FLAG:
				// Flag, Var, Final, Step
				if(table_index == KW_NEXT)
				{
					struct stack_for_frame *f = (struct stack_for_frame *)tempsp;
					// Is the the variable we are looking for?
					if(txtpos[-1] == f->for_var)
					{
						short int *varaddr = ((short int *)variables_table) + txtpos[-1] - 'A';
						write_int16(varaddr, read_int16(varaddr) + f->step);
						// Use a different test depending on the sign of the step increment
						if((f->step > 0 && read_int16(varaddr) <= f->terminal) ||
						   (f->step < 0 && read_int16(varaddr) >= f->terminal))
						{
							// We have to loop so don't pop the stack
							txtpos = f->txtpos;
							current_line = f->current_line;
							goto run_next_statement;
						}
						// We've run to the end of the loop. drop out of the loop, popping the stack
						sp = tempsp + sizeof(struct stack_for_frame);
						goto run_next_statement;
					}
				}
				// This is not the loop you are looking for... so Walk back up the stack
				tempsp += sizeof(struct stack_for_frame);
				break;
			default:
				goto stackstuffed;
		}
	}
	// Didn't find the variable we've been looking for
	goto syntaxerror;

assignment:
	{
		short int value;
		short int *var;

		if(*txtpos < 'A' || *txtpos > 'Z')
			goto syntaxerror;
		var = (short int *)variables_table + *txtpos - 'A';
		txtpos++;

		ignore_blanks();

		if (*txtpos != '=')
			goto syntaxerror;
		txtpos++;
		ignore_blanks();
		expression_error = 0;
		value = expression();
		if(expression_error)
			goto invalidexpr;
		// Check that we are at the end of the statement
		if(!check_statement_end())
			goto syntaxerror;
		//*var = value;
		write_int16(var, value);
	}
	goto run_next_statement;
poke:
	{
		short value;
		char* address;

		// Work out where to put it
		expression_error = 0;
		value = expression();
		if(expression_error)
			goto invalidexpr;
		address = (char*)((uintptr_t)value);

		// check for a comma
		ignore_blanks();
		if (*txtpos != ',')
			goto syntaxerror;
		txtpos++;
		ignore_blanks();

		// Now get the value to assign
		expression_error = 0;
		value = expression();
		if(expression_error)
			goto invalidexpr;

		// Do the write
                //EEPROM.write(address, value);
		printf("Poking %p value %d\n",address, (unsigned char)value);
		*address = value;

                // Check that we are at the end of the statement
		if(!check_statement_end())
			goto syntaxerror;

	}
	goto run_next_statement;

pinout:
	{
	    printmsg(unimplimentedmsg);
	    goto invalidexpr;
#if 0
		short int value;
		byte whichPin;

		// Work out where to put it
		expression_error = 0;
		value = expression();
		if(expression_error)
			goto invalidexpr;
		whichPin = value;

		// check for a comma
		ignore_blanks();
		if (*txtpos != ',')
			goto syntaxerror;
		txtpos++;
		ignore_blanks();

		// Now get the value to assign
		expression_error = 0;
		value = expression();
		if(expression_error)
			goto invalidexpr;

		pinMode(pinDef[whichPin], 1);	//Set to output
		pinDir[whichPin] = 1;
		pinStates[whichPin] = value;
		digitalWrite(pinDef[whichPin], pinStates[whichPin]);

		if(!check_statement_end())
			goto syntaxerror;
#endif
	}
	goto run_next_statement;

#if 0
pinmode:
	{
	    printmsg(unimplimentedmsg);
	    goto invalidexpr;
		short int value;
		byte whichPin;

		// Work out where to put it
		expression_error = 0;
		value = expression();
		if(expression_error)
			goto invalidexpr;
		whichPin = value;

		// check for a comma
		ignore_blanks();
		if (*txtpos != ',')
			goto syntaxerror;
		txtpos++;
		ignore_blanks();

		// Now get the value to assign
		expression_error = 0;
		value = expression();
		if(expression_error)
			goto invalidexpr;

		pinDir[whichPin] = value;						//Set value in array

		pinMode(pinDef[whichPin], pinDir[whichPin]);	//Set pin to that mode

		if (value == 1) {								//Are we making it an input?

			digitalWrite(pinDef[whichPin], 1);			//Write a 1 to enable pull up resistors

		}

		if(!check_statement_end())
			goto syntaxerror;
	}
	goto run_next_statement;
#endif

delayT:
	{
		int value;

		expression_error = 0;
		value = expression();
		if(expression_error)
			goto invalidexpr;
		delay(value);

		if(!check_statement_end())
			goto syntaxerror;
	}
	goto run_next_statement;

servo:
	{
	    printmsg(unimplimentedmsg);
	    goto invalidexpr;
#if 0
		byte whichPin;
		byte value;

		expression_error = 0;
		value = expression();
		if(expression_error)
			goto invalidexpr;
		whichPin = value;	//Which pin we're attaching the servo too.

		// check for a comma
		ignore_blanks();
		if (*txtpos != ',')
			goto syntaxerror;
		txtpos++;
		ignore_blanks();

		// Now get the value to assign
		expression_error = 0;
		value = expression();
		if(expression_error)
			goto invalidexpr;

		if (pinDir[whichPin] != 9) {					//Pin not already set for servo?

			myservo[whichPin].attach(pinDef[whichPin]);	//Attach servo[num] to that pin found in the mapping.

			pinDir[whichPin] = 9;						//Set flag that says a Servo is here

		}

		if (value == 255) {							//Flag to detach servo?

			myservo[whichPin].detach();				//Detach servo

			pinDir[whichPin] = 0;					//Clear pin status

		}

		if (value < 255) {							//Normal function?
			myservo[whichPin].write(value);
		}


		if(!check_statement_end())
			goto syntaxerror;
#endif
	}
	goto run_next_statement;

doPWM:
	{
	    printmsg(unimplimentedmsg);
	    goto invalidexpr;
#if 0
		short int value;
		byte whichPin;

		// Work out where to put it
		expression_error = 0;
		value = expression();
		if(expression_error)
			goto invalidexpr;
		whichPin = value;
		if (whichPin != 1 and whichPin != 3 and whichPin != 4)	//Not a PWM enabled pin?
			goto invalidexpr;										//Error

		// check for a comma
		ignore_blanks();
		if (*txtpos != ',')
			goto syntaxerror;
		txtpos++;
		ignore_blanks();

		// Now get the value to assign
		expression_error = 0;
		value = expression();
		if(expression_error)
			goto invalidexpr;

		pinMode(pinDef[whichPin], 1);	//Set to output
		pinDir[whichPin] = 1;
		pinStates[whichPin] = value;
		analogWrite(pinDef[whichPin], pinStates[whichPin]);

		if(!check_statement_end())
			goto syntaxerror;
#endif
	}
	goto run_next_statement;

list:
	linenum = testnum(); // Retuns 0 if no line found.

	// Should be EOL
	if(txtpos[0] != NL)
		goto syntaxerror;

	// Find the line
	list_line = findline();

	if (linenum != 0) {						//A specific line indicated?
		printline();						//Then print only it.
	}
	else {									//Else, print everything up to the end
		while(list_line != program_end)
			printline();
	}

	goto warmstart;

print:
	// If we have an empty list then just put out a NL
	if(*txtpos == ':' )
	{
	line_terminator();
		txtpos++;
		goto run_next_statement;
	}
	if(*txtpos == NL)
	{
		goto execnextline;
	}

	while(1)
	{
		ignore_blanks();
		if(print_quoted_string())
		{
			;
		}
		else if(*txtpos == '"' || *txtpos == '\'')
			goto syntaxerror;
		else
		{
			short int e;
			expression_error = 0;
			e = expression();
			if(expression_error)
				goto invalidexpr;
			printnum(e);
		}

		// At this point we have three options, a comma or a new line
		if(*txtpos == ',')
			txtpos++;	// Skip the comma and move onto the next
		else if(txtpos[0] == ';' && (txtpos[1] == NL || txtpos[1] == ':'))
		{
			txtpos++; // This has to be the end of the print - no newline
			break;
		}
		else if(check_statement_end())
		{
			line_terminator();	// The end of the print statement
			break;
		}
		else
			goto syntaxerror;
	}
	goto run_next_statement;
}

/***************************************************************************/
static void line_terminator(void)
{
	outchar(NL);
	//outchar(CR);
}


/***********************************************************/
// Keys read by breakcheck() that turned out not to be Ctrl-C. We cannot look
// at a key without taking it, so we keep them here, in order, for inchar()
// to return next -- rather than throwing away what the user typed ahead.
#define TYPEAHEAD 32
static char typeahead[TYPEAHEAD];
static int typeahead_len = 0;

static unsigned char breakcheck(void)
{
  // Look at every waiting key we have room for, not just the first: a
  // Ctrl-C typed after some other key must still stop the program. Once the
  // queue is full we stop, and leave the rest waiting in the kernel, in order.
  while(typeahead_len < TYPEAHEAD && basic_key_waiting()) {
    char c = basic_getc();
    if(c == CTRLC) {
      typeahead_len = 0;  // whatever was typed before it is moot now
      return 1;
    }
    typeahead[typeahead_len++] = c;
  }
  return 0;
}
/***********************************************************/
static int inchar()
{
    if(typeahead_len > 0) {
      char c = typeahead[0];
      for(int i = 1; i < typeahead_len; i++)
        typeahead[i - 1] = typeahead[i];
      typeahead_len--;
      return c;
    }
    return basic_getc();
}

/***********************************************************/
static void outchar(unsigned char c)
{
	basic_putc((char)c);
}

#if 0
static void lcdChar(uint8 c) {

	if (c == 8) {	//Backspace?

		if (cursorX > 0) {

			cursorX -= 1;	//Go back one
			screenMem[60 + cursorX] = 32;	//Erase it from memory
			doFrame(60 + cursorX); //Redraw screen up to that amount

		}

	}

	if (c != 13 and c != 10 and c != 8) { // Not a backspace or return, just a normal character

		screenMem[60 + cursorX] = c;
		cursorX += 1;
		if (cursorX < 20) {

			lcd.write(c);

		}

	}

	if (cursorX == 20 or c == 10) {	// Did we hit Enter or go type past the end of a visible line?

		// Lines are interlaced as described here: http://www.element14.com/community/message/81507/l/re-basic-pocket-computer-display-modification#81507
		for (int xg = 0 ; xg < 20 ; xg++) {

			screenMem[0 + xg] = screenMem[40 + xg];
			screenMem[40 + xg] = screenMem[20 + xg];
			screenMem[20 + xg] = screenMem[60 + xg];
			screenMem[60 + xg] = 32;


		}

		cursorX = 0;

		doFrame(80);

	}


}

static void doFrame(byte amount) {

	lcd.clear();
	lcd.noCursor();

	for (int xg = 0 ; xg < amount ; xg++) {

		lcd.write(screenMem[xg]);
	}

	lcd.cursor();

}
#endif


#define CHECK(name, cond) do {                                 \
    if (!(cond)) {                                             \
        printf("  FAIL: %s\n", (name));                        \
    }                                                          \
} while (0)


//***********************************************************/
int main()
{
  short int x = 0xaabb;
  short int y = read_int16(&x);

  CHECK("read", x == y);
  short int z = 0;
  write_int16(&z, 42);
  CHECK("write", z == 42);

  basic_start();

  while(1) {
    loop();
  }
}
/***********************************************************/
