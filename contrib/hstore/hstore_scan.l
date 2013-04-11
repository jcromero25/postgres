%{
static string scanstring;

/* No reason to constrain amount of data slurped */
/* #define YY_READ_BUF_SIZE 16777216 */

/* Handles to the buffer that the lexer uses internally */
static YY_BUFFER_STATE scanbufhandle;
static char *scanbuf;
static int	scanbuflen;

static void addstring(bool init, char *s, int l);
static void addchar(bool init, char s);

%}

%option 8bit
%option never-interactive
%option nodefault
%option noinput
%option nounput
%option noyywrap
%option warn
%option prefix="hstore_yy"
%option bison-bridge

%x xQUOTED
%x xNONQUOTED

any			[^\,\[\]\{\}\"\=\> \t\n\r\f\\]


%%

<INITIAL>[\,\{\}\[\]]			{ return *yytext; }

<INITIAL>\=\>					{ return DELIMITER_P; }

<INITIAL>[ \t\n\r\f]+			{ /* ignore */ }

<INITIAL>\=/[^\>]				{
									addchar(true, '=');
									BEGIN xNONQUOTED;
								}
									
<INITIAL>\>						{
									addchar(true, yytext[0]);
									BEGIN xNONQUOTED;
								}
<INITIAL>\\.					{
									addchar(true, yytext[1]);
									BEGIN xNONQUOTED;
								}

<INITIAL>({any}|\>)+			{
									addstring(true, yytext, yyleng);
									BEGIN xNONQUOTED;
								}
									
<INITIAL>\" 					{
									addchar(true, '\0');
									BEGIN xQUOTED;
								}

<INITIAL>\=						{	/* =<<EOF>> */
									addchar(true, '=');
									yylval->str = scanstring;
									return STRING_P;
								}

<xNONQUOTED>({any}|[\>\"])+		{ 
									addstring(false, yytext, yyleng); 
								}

<xNONQUOTED>\=/[^\>]			{ addchar(false, *yytext); }

<xNONQUOTED>[ \t\n\r\f]+		{ 
									yylval->str = scanstring;
									BEGIN INITIAL;
									return pg_strncasecmp("null", scanstring.val, scanstring.len) ? STRING_P : NULL_P;
								}

<xNONQUOTED>\=					{	/* =<<EOF>> */
									addchar(false, '=');
									yylval->str = scanstring;
									BEGIN INITIAL;
									return STRING_P;
								}

<xNONQUOTED>[\,\{\}\[\]]		{
									yylval->str = scanstring;
									yyless(0);
									BEGIN INITIAL;
									return pg_strncasecmp("null", scanstring.val, scanstring.len) ? STRING_P : NULL_P;
								}

<xNONQUOTED><<EOF>>				{ 
									yylval->str = scanstring;
									BEGIN INITIAL;
									return pg_strncasecmp("null", scanstring.val, scanstring.len) ? STRING_P : NULL_P;
								}

<xNONQUOTED>\=\>				{
									yylval->str = scanstring;
									yyless(0);
									BEGIN INITIAL;
									return pg_strncasecmp("null", scanstring.val, scanstring.len) ? STRING_P : NULL_P;
								}
									

<xNONQUOTED,xQUOTED>\\.  		{ addchar(false, yytext[1]); }

<INITIAL,xNONQUOTED,xQUOTED>\\ 	{ yyerror("Unexpected end after backslesh"); }

<xQUOTED><<EOF>>				{ yyerror("Unexpected end of quoted string"); }

<xQUOTED>\"						{
									yylval->str = scanstring;
									BEGIN INITIAL;
									return STRING_P;
								}

<xQUOTED>[^\\\"]+   			{ addstring(false, yytext, yyleng); }

<INITIAL><<EOF>>				{ yyterminate(); }

%%

void
yyerror(const char *message)
{
	if (*yytext == YY_END_OF_BUFFER_CHAR)
	{
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("bad hstore representation"),
				 /* translator: %s is typically "syntax error" */
				 errdetail("%s at end of input", message)));
	}
	else
	{
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("bad hstore representation"),
				 /* translator: first %s is typically "syntax error" */
				 errdetail("%s at or near \"%s\"", message, yytext)));
	}
}


/*
 * Called before any actual parsing is done
 */
static void
hstore_scanner_init(const char *str)
{
	Size	slen = strlen(str);

	/*
	 * Might be left over after ereport()
	 */
	if (YY_CURRENT_BUFFER)
		yy_delete_buffer(YY_CURRENT_BUFFER);

	/*
	 * Make a scan buffer with special termination needed by flex.
	 */

	scanbuflen = slen;
	scanbuf = palloc(slen + 2);
	memcpy(scanbuf, str, slen);
	scanbuf[slen] = scanbuf[slen + 1] = YY_END_OF_BUFFER_CHAR;
	scanbufhandle = yy_scan_buffer(scanbuf, slen + 2);

	BEGIN(INITIAL);
}


/*
 * Called after parsing is done to clean up after hstore_scanner_init()
 */
static void
hstore_scanner_finish(void)
{
	yy_delete_buffer(scanbufhandle);
	pfree(scanbuf);
}

static void
addstring(bool init, char *s, int l) {
	if (init) {
		scanstring.total = 32;
		scanstring.val = palloc(scanstring.total);
		scanstring.len = 0;
	}

	if (s && l) {
		while(scanstring.len + l + 1 >= scanstring.total) {
			scanstring.total *= 2;
			scanstring.val=realloc(scanstring.val, scanstring.total);
		}

		memcpy(scanstring.val+scanstring.len, s, l);
		scanstring.len+=l;
	}
}

static void
addchar(bool init, char s) {
	if (init) {
		scanstring.total = 32;
		scanstring.val = palloc(scanstring.total);
		scanstring.len = 0;
	}

	if (s != '\0') {
		if(scanstring.len + 2 >= scanstring.total) {
			scanstring.total*=2;
			scanstring.val=realloc(scanstring.val, scanstring.total);
		}

		scanstring.val[ scanstring.len++ ] = s;
	}
}

HStoreValue* 
parseHStore(const char *str) {
	HStoreValue		*parseresult;

	hstore_scanner_init(str);

	if (hstore_yyparse((void*)&parseresult) != 0)
		hstore_yyerror("bugus input");

	hstore_scanner_finish();

	return parseresult;
}
