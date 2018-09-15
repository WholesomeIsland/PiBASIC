#include "terminal.h"
#include "basic.h"
#include "ff.h"

extern "C"
{
#include <stdio.h>
#include <string.h>
#include <stdlib.h> 
#include "vga.h"
#include "linkedlist.h"


}

void basic_main()
{
	term_printf("\n                 The Raspberry Pi BASIC Development System");
	term_printf("\n                       Operating System Version 1.0");
	term_printf("\n                    Screen resolution: %d x %d - %dbpp");
	term_printf("\n\nReady.\n");

	struct Context ctx;
	unsigned char linebuf[LINE_SZ];
	int linenum, x;
	int dataPtr = 0;

	clear((unsigned char *)&ctx, sizeof(ctx));
	exec_init(&ctx);

	while (1)
	{
		get_input(linebuf);
		to_uppercase(linebuf);
		/* process line of code */
		if (ISDIGIT(linebuf[0]))
		{
			// get the full line number
			dataPtr = get_int(linebuf, 0, &linenum);
			while(linebuf[dataPtr] == ' ') dataPtr++;
			memcpy(linebuf, linebuf + dataPtr, length(linebuf) + dataPtr);

			// did user enter empty line number?
			if (isemptyline(linebuf))
				ll_delete(linenum);
			else
			{
				struct node* nodeLine = ll_find(linenum);
				unsigned char *data = (unsigned char *)malloc(sizeof(unsigned char) * length(linebuf)+1);
				memcpy(data, linebuf, length(linebuf)+1);

				// If the line doesnt exist, just add it.
				// otherwise free the previous data memory and add the new line
				if (nodeLine == NULL)
					ll_insertFirst(linenum, data);
				else
				{
					free(nodeLine->data);
					nodeLine->data = data;
				}	
			}

			continue;
		}

		/* process interpreter command */
		if (ISALPHA(linebuf[0]))
		{
			if (compare(linebuf, (unsigned char*)"RUN"))
			{
				exec_program(&ctx);
			}
			else
				if (compare(linebuf, (unsigned char*)"LIST"))
				{
					
					exec_cmd_list();
				}
			else
			{
				term_printf("\n");
				if (compare(linebuf, (unsigned char*)"NEW"))
				{
					exec_cmd_new(&ctx);
				}	
				else
					if(compare(linebuf, (unsigned char*)"DIR"))
					{
						exec_cmd_dir();
					}
				else
					if(strncmp((char*)linebuf, "LOAD ", 5) == 0)
					{
						int t = 0;
						while(linebuf[t] != 0)
						{
							linebuf[0+t] = linebuf[5+t];
							t++;
						}
						exec_cmd_load(linebuf);
					}	
				else
					if(strncmp((char *)linebuf, "SAVE ", 5) == 0)
					{
						int t = 0;
						while(linebuf[t] != 0)
						{
							linebuf[0+t] = linebuf[5+t];
							t++;
						}
						exec_cmd_save(linebuf);
					}
					else
					{
						term_printf("?Syntax Error");
					}
			}
			term_printf("\nReady.\n");
		}
	}
}

void exec_init(struct Context *ctx)
{
	BINDCMD(&ctx->cmds[0], "PRINT", exec_cmd_print, TOKEN_PRINT);
	BINDCMD(&ctx->cmds[1], "INPUT", exec_cmd_input, TOKEN_INPUT);
	BINDCMD(&ctx->cmds[2], "RETURN", exec_cmd_return, TOKEN_RETURN);
	BINDCMD(&ctx->cmds[3], "GOTO", exec_cmd_goto, TOKEN_GOTO);
	BINDCMD(&ctx->cmds[4], "GOSUB", exec_cmd_gosub, TOKEN_GOSUB);
	BINDCMD(&ctx->cmds[5], "LET", exec_cmd_let, TOKEN_LET);
	BINDCMD(&ctx->cmds[6], "END", exec_cmd_end, TOKEN_END);
	BINDCMD(&ctx->cmds[7], "STOP", exec_cmd_end, TOKEN_STOP);
	BINDCMD(&ctx->cmds[8], "IF", exec_cmd_if, TOKEN_IF);
	BINDCMD(&ctx->cmds[9], "DIM", exec_cmd_dim, TOKEN_DIM);
	BINDCMD(&ctx->cmds[10], "REM", exec_cmd_rem, TOKEN_REM);
	BINDCMD(&ctx->cmds[11], "THEN", exec_cmd_then, TOKEN_THEN);
}

void exec_program(struct Context* ctx)
{
	int ci;

	ll_sort();

	struct node *currentNode = ll_gethead();

	ctx->running = true;
	ctx->jmpline = -1;
	ctx->line = 0;
	ctx->dsptr = 0;
	ctx->csptr = 0;
	ctx->allocated = 0;
	ctx->error = ERR_NONE;

	var_clear_all(ctx);

	while (ctx->running && currentNode != NULL)
	{
		ctx->original_line = currentNode->data;

		unsigned char* tokenized_line = (unsigned char*)malloc(sizeof(unsigned char) * 160);
		tokenize(ctx, ctx->original_line, tokenized_line);

		ctx->tokenized_line = tokenized_line;
		ctx->line = currentNode->linenum;

		exec_line(ctx);

		free(ctx->tokenized_line);
		
		char ch = term_getchar();
		
		if (ch == 27)
		{
			ctx->running = false;
			ctx->error = ERR_UNEXP;
			term_printf("\n?Break in %d", ctx->line);
			return;
		}

		if (ctx->jmpline == -1)
		{
			currentNode = currentNode->next;
			ctx->linePos = 0;
		}	
		else
		{
			// error if jump line doesnt exist
			if (ll_find(ctx->jmpline) == NULL)
			{
				ctx->running = false;
				ctx->error = ERR_UNEXP;
				term_printf("\n?Undef'd statement error in %d", ctx->line);
			}
			else
			{
				currentNode = ll_find(ctx->jmpline);
				ctx->jmpline = -1;
			}
		}
	}

}

void exec_line(struct Context *ctx)
{
	int origLinePos;
	int j;
	unsigned char cmd[CMD_NAMESZ];
	bool foundCmd = false;

	while(true)
	{
		foundCmd = false;
		ctx->linePos = ignore_space(ctx->tokenized_line, ctx->linePos);

		if (ctx->linePos == -1)
		{
			break;
		}

		origLinePos = ctx->linePos;
		ctx->linePos = get_symbol(ctx->tokenized_line, ctx->linePos, cmd);

		for (j = 0; j < CMD_COUNT; j++)
		{
			if (cmd[0] == ctx->cmds[j].token)
			{
				foundCmd = true;
				// execute the statement
				ctx->cmds[j].func(ctx);
				break;
			}
		}

		// Assume LET for anything else
		if (!foundCmd)
		{
			ctx->linePos = origLinePos;
			exec_cmd_let(ctx);
		}

		if (ctx->error != ERR_NONE)
		{
			ctx->running = false;
			return;
		}

		if (ctx->jmpline != -1)
			break;

		if (ctx->linePos != -1)
		{
			if (ctx->tokenized_line[ctx->linePos] != TOKEN_THEN)
				ctx->linePos = next_statement(ctx);
			else
				ctx->linePos++;
		}
	}
}

int exec_expr(struct Context *ctx)
{
	unsigned char name[6];
	char pending = '='; /* operation that is run on an operand */
	bool operand;       /* this flag marks if an operand was parsed */
	float value;
	int j;

	while (true)
	{
		operand = false;
		ctx->linePos = ignore_space(ctx->tokenized_line, ctx->linePos);

		if (ctx->linePos == -1 || 
			ctx->tokenized_line[ctx->linePos] == ':' ||
			ctx->tokenized_line[ctx->linePos] == ';' ||
			ctx->tokenized_line[ctx->linePos] == ',' ||
			ctx->tokenized_line[ctx->linePos] > 127 )
			break;

		if (ISDIGIT(ctx->tokenized_line[ctx->linePos]))
		{
			ctx->linePos = get_float(ctx->tokenized_line, ctx->linePos, &value);
			operand = true;
		}
		else
			if (ISALPHA(ctx->tokenized_line[ctx->linePos]))
			{
				ctx->linePos = get_symbol(ctx->tokenized_line, ctx->linePos, name);

				//if (compare(name, (unsigned char*)"THEN"))
				//	break;

				for (j = 0; j < ctx->var_count; j++)
				{
					if (compare(ctx->vars[j].name, name) && ctx->vars[j].type == VAR_FLOAT)
					{
						value = *((float*)ctx->vars[j].location);
						operand = true;
						break;
					}
					if (compare(ctx->vars[j].name, name) && (ctx->vars[j].type == VAR_INT))
					{
						value = *((int*)ctx->vars[j].location);
						operand = true;
						break;
					}
				}
				if (!operand)
				{
					if (ctx->dsptr == 0)
					{
						ctx->dsptr++;
						ctx->dstack[ctx->dsptr] = 0;
					}
					else
						ctx->dstack[ctx->dsptr] = (ctx->dstack[ctx->dsptr] == 0);

					var_add_update_float(ctx, name, ctx->dstack[ctx->dsptr--]); //, 1, 0);

					for (j = 0; j < ctx->var_count; j++)
					{
						if (compare(ctx->vars[j].name, name) && ctx->vars[j].type == VAR_FLOAT)
						{
							value = *((float*)ctx->vars[j].location);
							operand = true;
							break;
						}
					}
				}
				else
				{
					int w = ignore_space(ctx->tokenized_line, ctx->linePos);
					if (ctx->tokenized_line[w] == '(')
					{
						/* push array reference to data stack */
						ctx->linePos = w;
						ctx->dstack[++ctx->dsptr] = (int)pending;
						//ctx->dstack[++ctx->dsptr] = &((void *)ctx->vars[j].location); 
						ctx->dstack[++ctx->dsptr] = (int) '?';
						pending = ctx->tokenized_line[ctx->linePos++];
						operand = false;
					}
				}
			}
			else
				if (ISOP(ctx->tokenized_line[ctx->linePos]))
				{
					if (ctx->tokenized_line[ctx->linePos] == '(')
					{
						ctx->dsptr++;
						ctx->dstack[ctx->dsptr] = (int)pending;
						pending = ctx->tokenized_line[ctx->linePos++];
					}
					else
						if (ctx->tokenized_line[ctx->linePos] == ')')
						{
							value = ctx->dstack[ctx->dsptr--];
							pending = (char)ctx->dstack[ctx->dsptr--];

							if (pending == '?')
							{
								/* dereference an array */
								int index = ctx->dstack[ctx->dsptr--];
								value = *((float*)ctx->vars[j].location + index);
								pending = (char)ctx->dstack[ctx->dsptr--];
							}

							operand = true;
							/* bypass closing parenthesis */
							ctx->linePos++;
						}
						else
						{
							pending = ctx->tokenized_line[ctx->linePos++];
						}
				}
				else
					if (ctx->tokenized_line[ctx->linePos] == ']')
					{
						ctx->linePos++;
						break;
					}
					else
					{
						printf("\n?Syntax error in %d", ctx->line);
						ctx->running = false;
						ctx->error = ERR_UNEXP;
						ctx->linePos++;
						break;
					}

		/* process pending operation with the operand value */
		if (operand)
		{
			switch (pending)
			{
				case '=':
					if (ctx->dsptr == 0)
					{
						ctx->dsptr++;
						ctx->dstack[ctx->dsptr] = value;
					}
					else
						ctx->dstack[ctx->dsptr] = (ctx->dstack[ctx->dsptr] == value);
					break;
				case '+': 
					ctx->dstack[ctx->dsptr] += value; 
					break;
				case '-': ctx->dstack[ctx->dsptr] -= value;	break;
				case '*':
					ctx->dstack[ctx->dsptr] *= value;
					break;
				case '/':
					if (value == 0)
					{
						term_printf("\n?Division by zero error in %d", ctx->line);
						ctx->running = false;
						ctx->error = ERR_UNEXP;
					}
					else
						ctx->dstack[ctx->dsptr] /= value;
					break;
				//case '%': ctx->dstack[ctx->dsptr] %= value; break;
				case '>': ctx->dstack[ctx->dsptr] = ctx->dstack[ctx->dsptr] > value; break;
				case '<': ctx->dstack[ctx->dsptr] = ctx->dstack[ctx->dsptr] < value; break;
				case '(':
					ctx->dsptr++;
					ctx->dstack[ctx->dsptr] = value;
					break;
			}	
		}
	}
	return ctx->linePos;
}

void exec_cmd_dim(struct Context *ctx)
{
	unsigned char name[VAR_NAMESZ];

	ctx->linePos = ignore_space(ctx->tokenized_line, ctx->linePos);
	ctx->linePos = get_symbol(ctx->tokenized_line, ctx->linePos, name);
	exec_expr(ctx);
	//var_add_update(ctx, name, 0, ctx->dstack[ctx->dsptr--], 0);
}

void exec_cmd_dir()
{
	term_printf("\nFiles:\n");
	
	DIR dir;
	FILINFO filImage;
	FRESULT res;
	char* ext;
	
	res = f_opendir(&dir, ".");
	if (res == FR_OK)
	{
		do 
		{
			res = f_readdir(&dir, &filImage);
			ext = strrchr(filImage.fname, '.');
			if (res == FR_OK && filImage.fname[0] != 0)
			{
				term_printf("%s\n",filImage.fname);
			}
		}
		while (res == FR_OK && filImage.fname[0] != 0);
		
		f_closedir(&dir);
	}
	else
		term_printf("\n?Disk read error.");

	term_printf("\n");
}

void exec_cmd_end(struct Context *ctx)
{
	ctx->running = false;
}

void exec_cmd_gosub(struct Context *ctx)
{
	exec_expr(ctx);

	// store line number and next statement pos on stack
	ctx->linePos = next_statement(ctx);

	ctx->cstack[++ctx->csptr] = ctx->line;
	ctx->cstack[++ctx->csptr] = ctx->linePos;

	// set jump flag
	ctx->jmpline = ctx->dstack[ctx->dsptr--];
	ctx->linePos = 0;
}

void exec_cmd_goto(struct Context *ctx)
{
	exec_expr(ctx);
	ctx->linePos = 0;
	ctx->jmpline = ctx->dstack[ctx->dsptr--];
}

void exec_cmd_if(struct Context *ctx)
{
	ctx->linePos = exec_expr(ctx);

	ctx->linePos = ignore_space(ctx->tokenized_line, ctx->linePos);

	if (ctx->tokenized_line[ctx->linePos] != TOKEN_THEN)
	{
		term_printf("\n?Syntax error in %d", ctx->line);
		ctx->running = false;
		ctx->error = ERR_UNEXP;
	}

	if (ctx->dstack[ctx->dsptr--])
		return;
	else
		ctx->linePos = -1;
}

void exec_cmd_input(struct Context *ctx)
{
	unsigned char name[VAR_NAMESZ+3], ch;
	float value = 0;
	int j;

	ctx->linePos = ignore_space(ctx->tokenized_line, ctx->linePos);
	if (ctx->tokenized_line[ctx->linePos] == '\"')
	{
		ctx->linePos++;

		int tempStackPtr = ctx->dsptr;

		while (ctx->tokenized_line[ctx->linePos] != '\"')
		{
			// push string to stack
			ctx->dstack[ctx->dsptr++] = ctx->tokenized_line[ctx->linePos++];
		}

		/* ignore quotation mark */
		ctx->linePos++;

		/* ensure semicolon */
		if (ctx->tokenized_line[ctx->linePos] != ';')
		{
			term_printf("\n?Syntax error in %d", ctx->line);
			ctx->running = false;
			ctx->error = ERR_UNEXP;
			return;
		}

		/* pop the stack, print the prompt */
		for(int ctr=tempStackPtr; ctr<ctx->dsptr; ctr++)
		{
			term_putchar(ctx->dstack[ctr]);
		}

		ctx->dsptr = tempStackPtr;
	}

	ctx->linePos = ignore_space(ctx->tokenized_line, ctx->linePos);

	/* ignore semicolon */
	if (ctx->tokenized_line[ctx->linePos] == ';')
	{
		ctx->linePos++;
		ctx->linePos = ignore_space(ctx->tokenized_line, ctx->linePos);
	}

	ctx->linePos = get_symbol(ctx->tokenized_line, ctx->linePos, name);

	ch = 0;

	unsigned char buffer[160] = { 0 };
	int bufferctr = 0;

	term_printf("? ");
	
	while (true)
	{
		char ch = term_getchar();

		if (ch != 0)
		{
			term_putchar(ch);

			if (ch == 27)
			{
				ctx->running = false;
				ctx->error = ERR_UNEXP;
				term_printf("\n?Break in %d", ctx->line);
				return;
			}

			if (ch == '\n')
			{
				buffer[bufferctr] = '\0';
				break;
			}
			else if (ch == 8)
			{
				if (bufferctr > 0)
					bufferctr--;
			}
			else
				buffer[bufferctr++] = ch;
		}
	}

	get_float(buffer, 0, &value);

	var_add_update_float(ctx, name, value);
	
	ctx->linePos = ignore_space(ctx->tokenized_line, ctx->linePos);
	
	// anything after expression must be end of line or colon
	if (ctx->linePos != -1 && ctx->tokenized_line[ctx->linePos] != ':')
	{
		ctx->running = false;
		ctx->error = ERR_UNEXP;
		printf("\n?Syntax error in %d", ctx->line);
		return;
	}
}

void exec_cmd_let(struct Context *ctx)
{
	unsigned char name[VAR_NAMESZ+2];  // 2 chars, var type, \0
	int j = 0;

	ctx->linePos = ignore_space(ctx->tokenized_line, ctx->linePos);
	ctx->linePos = get_symbol(ctx->tokenized_line, ctx->linePos, name);
	ctx->linePos = ignore_space(ctx->tokenized_line, ctx->linePos);

	if (ctx->tokenized_line[ctx->linePos] == '[')
	{
		ctx->linePos = exec_expr(ctx);
		ctx->linePos = ignore_space(ctx->tokenized_line, ctx->linePos);
		j = ctx->dstack[ctx->dsptr--];
	}

	if (ctx->tokenized_line[ctx->linePos] != '=')
	{
		ctx->running = false;
		ctx->error = ERR_UNEXP;
		term_printf("\n?Syntax error in %d", ctx->line);
		return;
	}

	exec_expr(ctx);

	ctx->linePos = ignore_space(ctx->tokenized_line, ctx->linePos);

	// anything after expression must be end of line or colon
	if (ctx->linePos != -1 && ctx->tokenized_line[ctx->linePos] != ':')
	{
		ctx->running = false;
		ctx->error = ERR_UNEXP;
		term_printf("\n?Syntax error in %d", ctx->line);
		return;
	}

	if (name[length(name)-1] == '%')
		var_add_update_int(ctx, name, ctx->dstack[ctx->dsptr--]);
	else
		var_add_update_float(ctx, name, ctx->dstack[ctx->dsptr--]);

}

void exec_cmd_list()
{
	ll_sort();

	struct node *ptr = ll_gethead();
	term_printf("\n");

	//start from the beginning
	while (ptr != NULL)
	{
		char lnbuff[10] = { 0 };
		snprintf(lnbuff, sizeof(lnbuff), "%d", ptr->linenum);
		term_printf("%s %s\n", lnbuff, ptr->data);
		ptr = ptr->next;
	}
}

void exec_cmd_load(unsigned char* filename)
{
	FIL fp;
	FRESULT res;
	UINT bytesRead;
	BYTE b[2];
	char buffer[160];
	int bufferCtr = 0;
	int linenum = 0;
	int dataPtr = 0;
	
	// new cmd
	while(!ll_isEmpty())
		ll_deleteFirst();
	
	char fnbuffer[15] = {0};
	snprintf(fnbuffer, sizeof(fnbuffer), "%s.BAS", filename);
	
	term_printf("Searching for %s\n", filename);
	
	res = f_open(&fp, fnbuffer, FA_READ | FA_OPEN_EXISTING);
	
	if (res == FR_OK)
	{
		term_printf("Loading\n");
		
		while (true) 
		{
			res = f_read(&fp, b, 1, &bytesRead);
			
			if(res == FR_OK && bytesRead == 1)
			{
				if(b[0] != '\n')
					buffer[bufferCtr++] = b[0];
				else 
				{
					if(bufferCtr != 0)
					{
						buffer[bufferCtr] = 0;
						dataPtr = get_int((unsigned char *)buffer, 0, &linenum);
						dataPtr = ignore_space((unsigned char *)buffer, dataPtr);
						memcpy(buffer, buffer + dataPtr, strlen(buffer)+dataPtr);						
						char *data = (char *)malloc(sizeof(char) * strlen(buffer) + 1);
						memcpy(data, buffer, strlen(buffer) + 1);
						ll_insertFirst(linenum, (unsigned char *)data);
						bufferCtr=0;
					}
				}
			}
			else
			{
				if(bufferCtr != 0)
				{
					buffer[bufferCtr] = 0;
					dataPtr = get_int((unsigned char *)buffer, 0, &linenum);
					dataPtr = ignore_space((unsigned char *)buffer, dataPtr);
					memcpy(buffer, buffer + dataPtr, strlen(buffer)+dataPtr);
					char *data = (char *)malloc(sizeof(char) * strlen(buffer) + 1);
					memcpy(data, buffer, strlen(buffer) + 1);
					ll_insertFirst(linenum, (unsigned char *)data);
					bufferCtr=0;
				}
				break;
			}
		};

		ll_sort();
		f_close(&fp);
	}
	else
		term_printf("?File load error #%d", res);
}

void exec_cmd_new(struct Context *ctx)
{
	while (!ll_isEmpty())
		ll_deleteFirst();

	var_clear_all(ctx);
}

void exec_cmd_print(struct Context *ctx)
{
	bool eol = true;
	bool quoteMode = false;

	while (true)
	{
		if (quoteMode == false)
			ctx->linePos = ignore_space(ctx->tokenized_line, ctx->linePos);

		if (ctx->linePos == -1 || ctx->tokenized_line[ctx->linePos] == ':')
			break;

		if (ctx->tokenized_line[ctx->linePos] == '\"' && quoteMode == false)
		{
			quoteMode = true;
			ctx->linePos++;	// skip quote

			while (ctx->tokenized_line[ctx->linePos] != '\"' && ctx->tokenized_line[ctx->linePos] != '\0')
				term_putchar(ctx->tokenized_line[ctx->linePos++]);

			if (ctx->tokenized_line[ctx->linePos] == '\"')
			{
				ctx->linePos++; // skip quote
				quoteMode = false;
				ctx->linePos = ignore_space(ctx->tokenized_line, ctx->linePos);
			}
		}
		else
		{
			ctx->linePos = exec_expr(ctx);
			if (ctx->error == ERR_NONE)
				term_printf("%g", (double)ctx->dstack[ctx->dsptr--]);
			else
				break;

		}

		if (ctx->tokenized_line[ctx->linePos] == ';')
		{
			ctx->linePos++;
			eol = false;
		}
		
		if (ctx->tokenized_line[ctx->linePos] == ',')
		{
			ctx->linePos++;
			term_printf("     ");
		}
	}

	if (eol) term_putchar('\n');
}

void exec_cmd_rem(struct Context *ctx)
{
	// skip this line (do nothing)
	return;
}

void exec_cmd_return(struct Context *ctx)
{
	if (ctx->csptr == 0)
	{
		ctx->running = false;
		ctx->error = ERR_UNEXP;
		term_printf("\n?RETURN without GOSUB in %d", ctx->line);
		return;
	}

	// release current line resource
	free(ctx->tokenized_line);
	
	// Get previous line pos and line number from stack
	ctx->linePos = ctx->cstack[ctx->csptr--];
	ctx->line = ll_find(ctx->cstack[ctx->csptr])->linenum;
	ctx->original_line = ll_find(ctx->cstack[ctx->csptr--])->data;
	
	// retokenize the line
	unsigned char* tokenized_line = (unsigned char*)malloc(sizeof(unsigned char)*160);
	tokenize(ctx, ctx->original_line, tokenized_line);

	ctx->tokenized_line = tokenized_line;
	ctx->jmpline = ctx->line;
}

void exec_cmd_save(unsigned char* filename)
{
	FIL fp;
	FRESULT res;

	char fnbuffer[15] = {0};
	snprintf(fnbuffer, sizeof(fnbuffer), "%s.BAS", filename);
	
	res = f_open(&fp, fnbuffer, FA_WRITE | FA_CREATE_NEW);
	
	if (res == FR_OK)
	{
		term_printf("Saving %s\n", filename);
		
		ll_sort();
		struct node *ptr = ll_gethead();

		//start from the beginning
		while (ptr != NULL) 
		{
			char lnbuff[160] = { 0 };
			snprintf(lnbuff, sizeof(lnbuff), "%d %s", ptr->linenum, ptr->data);
			f_puts(lnbuff, &fp);
			ptr = ptr->next;
		}
		
		f_close(&fp);
	}
	else
		term_printf("?File Save Error #%d", res);
}

void exec_cmd_then(struct Context *ctx)
{
	// skip this line (do nothing)
	return;
}

void var_clear_all(struct Context *ctx)
{
	for (int j = 0; j < ctx->var_count; j++)
	{
		ctx->vars[j].name[0] = 0;
		ctx->vars[j].type = VAR_NONE;
		free(ctx->vars[j].location);
	}

	ctx->var_count = 0;
}

void var_add_update_int(struct Context *ctx, const unsigned char *key, int value)
{
	int j;

	// find and update the variable
	for (j = 0; j < ctx->var_count; j++)
	{
		if (compare(ctx->vars[j].name, key) && ctx->vars[j].type == VAR_INT)
		{
			free(ctx->vars[j].location);
			ctx->vars[j].location = (int *)malloc(sizeof(int));
			*((int*)ctx->vars[j].location) = value;
			return;
		}
	}
	
	// variable not found.  add it
	ctx->var_count = j+1;
	strcpy((char *)ctx->vars[ctx->var_count-1].name, (char *)key);
	ctx->vars[ctx->var_count-1].location = (int *)malloc(sizeof(int));
	ctx->vars[ctx->var_count-1].type = VAR_INT;
	*((int*)ctx->vars[ctx->var_count-1].location) = value;
	
}

void var_add_update_float(struct Context *ctx, const unsigned char *key, float value)
{
	int j;

	// find and update the variable
	for (j = 0; j < ctx->var_count; j++)
	{
		if (compare(ctx->vars[j].name, key) && ctx->vars[j].type == VAR_FLOAT)
		{
			free(ctx->vars[j].location);
			ctx->vars[j].location = (float *)malloc(sizeof(float));
			*((float*)ctx->vars[j].location) = value;
			return;
		}
	}

	// variable not found.  add it
	ctx->var_count = j + 1;
	strcpy((char *)ctx->vars[ctx->var_count - 1].name, (char *)key);
	ctx->vars[ctx->var_count - 1].location = (float *)malloc(sizeof(float));
	ctx->vars[ctx->var_count - 1].type = VAR_FLOAT;
	*((float*)ctx->vars[ctx->var_count - 1].location) = value;

}

// sbparse

int get_symbol(const unsigned char *s, int i, unsigned char *t)
{
	while (s[i] != '\0')
	{
		if (s[i] > 127)
		{
			*(t++) = s[i++];
			break;
		}

		if (ISALPHA(s[i]))
			*(t++) = s[i++];
		else
			break;
	}

	// string
	if (s[i] == '$')
		*(t++) = s[i++];

	// int
	if (s[i] == '%')
		*(t++) = s[i++];
	
	*t = 0;
	return i;
}

int get_int(const unsigned char *s, int i, int *num)
{
	*num = 0;
	while (s[i] && ISDIGIT(s[i]))
	{
		*num *= 10;
		*num += s[i++] - '0';
	}
	return i;
}

int get_float(const unsigned char* s, int i, float *fv)
{
	float rez = 0, fact = 1;

	if (i == 0 && s[i] == '-')
	{
		i++;
		fact = -1;
	};

	for (int point_seen = 0; ISDIGIT(s[i]) || (s[i] == '.' && point_seen == 0); i++)
	{
		if (s[i] == '.' && point_seen == 0)
		{
			point_seen = 1;
			continue;
		}

		int d = s[i] - '0';

		if (d >= 0 && d <= 9)
		{
			if (point_seen) fact /= 10.0f;
			rez = rez * 10.0f + (float)d;
		};
	};

	*fv = rez * fact;
	return i;
};

int ignore_space(const unsigned char *s, int i)
{
	while (s[i] && ISSPACE(s[i]))
	{
		i++;
	}

	if (s[i] == 0)
		return -1;
	else
		return i;
}

void to_uppercase(unsigned char *s)
{
	bool toggle = true;
	while (*s)
	{
		if (ISalpha(*s))
		{
			if (toggle)
				*s = *s - 32;
		}
		else
			if (*s == '\"')
			{
				toggle = !toggle;
			}
			else
				if (*s == '\'')
				{
					toggle = false;
				}
				else
					if (*s == '\n')
					{
						toggle = true;
					}
		s++;
	}
}

bool isemptyline(unsigned char *linebuf)
{
	bool emptyline = true;
	int ctr = 0;
	while (ISDIGIT(linebuf[ctr]))
	{
		ctr++;
	}

	while (linebuf[ctr] != '\0')
	{
		if (!ISSPACE(linebuf[ctr]))
		{
			emptyline = false;
			break;
		}
		ctr++;
	}

	return emptyline;
}

bool compare(const unsigned char *a, const unsigned char *b)
{
	if (a == false && b == false) return true;
	if (a == false || b == false) return false;
	while (*a && *b && (*a == *b))
	{
		a++;
		b++;
	}
	if ((*a == '\0') && (*b == '\0')) return true;
	return false;
}

void clear(unsigned char *dst, int size)
{
	for (; size>0; size--, dst++)
	{
		*dst = 0;
	}
}

int length(const unsigned char *s)
{
	int l = 0;
	while (*(s++)) l++;
	return l;
}

void join(unsigned char *dst, const unsigned char *src)
{
	strcpy((char *)(dst + strlen((char *)dst)), (char *)src);
}

void get_input(unsigned char *s)
{
	int inbufferctr = 0;

	while (true)
	{
		char c = term_getchar();

		if (c != 0)
		{
			term_putchar(c);

			if (c == '\n')
			{
				s[inbufferctr] = '\0';
				break;
			}
			else if (c == 8)
			{
				if (inbufferctr > 0)
					inbufferctr--;
			}
			else
				s[inbufferctr++] = c;
		}
	}
}

int next_statement(struct Context *ctx)
{
	int ctr = ctx->linePos;
	int quoteMode = 0;

	while (true)
	{
		if (ctx->tokenized_line[ctr] == '\0' || (ctx->tokenized_line[ctr] == ':' && quoteMode == 0))
			break;

		if (ctx->tokenized_line[ctr] == '\"')
		{
			if (quoteMode == 0)
				quoteMode = 1;
			else
				quoteMode = 0;
		}
		ctr++;
	}

	if (ctx->tokenized_line[ctr] == ':')
		ctr++;
	else
		ctr = -1;

	return ctr;
}

void tokenize(struct Context* ctx, const unsigned char* input, unsigned char *output)
{
	int i = 0, o = 0, tempStackPtr = 0;
	bool quoteMode = false;
	bool foundCmd = false;
	unsigned char tempStack[160] = { 0 };

	while (true)
	{
		if (input[i] == 0 || input[i] == ':')
		{
			if (tempStackPtr != 0)
			{
				for (int x = 0; x < tempStackPtr; x++)
					output[o++] = tempStack[x];
				tempStackPtr = 0;
			}

			output[o] = input[i];

			if (input[i] == 0)
				break;
			else
			{
				i++;
				o++;
				continue;
			}
		}

		if (quoteMode == false)
		{
			if (ISALPHA(input[i]))
			{
				// push to the stack
				tempStack[tempStackPtr++] = input[i++];
				tempStack[tempStackPtr] = 0;

				for (int j = 0; j < CMD_COUNT; j++)
				{
					if (compare(tempStack, ctx->cmds[j].name))
					{
						output[o++] = ctx->cmds[j].token;
						tempStackPtr = 0;
						break;
					}
				}
			}
			else
			{
				if (input[i] == '\"')
					quoteMode = true;

				tempStack[tempStackPtr++] = input[i++];

				for (int x = 0; x < tempStackPtr; x++)
					output[o++] = tempStack[x];
				
				tempStackPtr = 0;
			}
		}



		if (quoteMode == true)
		{
			if(input[i] == '\"')
				quoteMode = false;
			
			output[o++] = input[i++];
		}
	};

	
	to_uppercase(output);
}

