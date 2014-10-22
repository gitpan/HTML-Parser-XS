/* $Id: hparser.c,v 2.14 1999/12/05 21:50:09 gisle Exp $
 *
 * Copyright 1999, Gisle Aas.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the same terms as Perl itself.
 */

#ifndef EXTERN
#define EXTERN extern
#endif

#include "hctype.h"    /* isH...() macros */
#include "tokenpos.h"  /* dTOKEN; PUSH_TOKEN() */


static
struct literal_tag {
  int len;
  char* str;
}
literal_mode_elem[] =
{
  {6, "script"},
  {5, "style"},
  {3, "xmp"},
  {9, "plaintext"},
  {0, 0}
};

/*
 * Parser functions.
 *
 *   parse()                       - top level entry point.
 *                                   deals with text and calls one of its
 *                                   subordinate parse_*() routines after
 *                                   looking at the first char after "<"
 *     parse_decl()                - deals with declarations         <!...>
 *       parse_comment()           - deals with <!-- ... -->
 *       parse_marked_section      - deals with <![ ... [ ... ]]>
 *     parse_end()                 - deals with end tags             </...>
 *     parse_start()               - deals with start tags           <A...>
 *     parse_process()             - deals with process instructions <?...>
 *     parse_null()                - deals with anything else        <....>
 *
 *     report_event() - called whenever any of the parse*() routines
 *                      has recongnized something.
 */

static void
report_event(PSTATE* p_state,
	     event_id_t event,
	     char *beg, char *end,
	     token_pos_t *tokens, int num_tokens,
	     STRLEN offset,
	     SV* self
	    )
{
  struct p_handler *h = &p_state->handlers[event];
  dSP;
  AV *array;
  STRLEN my_na;
  char *attrspec;
  char *s;

  if (0) {  /* used for debugging at some point */
    char *s = beg;
    int i;

    /* print debug output */
    switch(event) {
    case E_DECLARATION: printf("DECLARATION"); break;
    case E_COMMENT:     printf("COMMENT"); break;
    case E_START:       printf("START"); break;
    case E_END:         printf("END"); break;
    case E_TEXT:        printf("TEXT"); break;
    case E_PROCESS:     printf("PROCESS"); break;
    default:            printf("EVENT #%d", event); break;
    }

    printf(" [");
    while (s < end) {
      if (*s == '\n') {
	putchar('\\'); putchar('n');
      }
      else
	putchar(*s);
      s++;
    }
    printf("] %d\n", end - beg);
    for (i = 0; i < num_tokens; i++) {
      printf("  token %d: %d %d\n",
	     i,
	     tokens[i].beg - beg,
	     tokens[i].end - tokens[i].beg);
    }
  }

#ifdef MARKED_SECTION
  if (p_state->ms == MS_IGNORE)
    return;
#endif

  if (!h->cb) {
    /* event = E_DEFAULT; */
    h = &p_state->handlers[E_DEFAULT];
    if (!h->cb)
      return;
  }

  if (SvTYPE(h->cb) == SVt_PVAV) {
    /* start sub-array for accumulator array */
    array = newAV();
  }
  else {
    array = 0;
    /* start argument stack for callback */
    ENTER;
    SAVETMPS;
    PUSHMARK(SP);
  }

  attrspec = SvPV(h->attrspec, my_na);

  for (s = attrspec; *s; s++) {
    SV* arg = 0;
    switch(*s) {
    case 's':
      arg = self;
      break;

    case 't':
      /* tokens arrayref */
      if (num_tokens >= 1) {
	AV* av = newAV();
	SV* prev_token;
	int i;
	av_extend(av, num_tokens);
	for (i = 0; i < num_tokens; i++) {
	  if (tokens[i].beg) {
	    prev_token = newSVpvn(tokens[i].beg, tokens[i].end-tokens[i].beg);
	    av_push(av, prev_token);
	  }
	  else { /* boolean */
	    av_push(av, p_state->bool_attr_val
		          ? newSVsv(p_state->bool_attr_val)
		          : newSVsv(prev_token));
	  }
	}
	arg = sv_2mortal(newRV_noinc((SV*)av));
      }
      break;

    case '#':
      /* tokenpos arrayref */
      if (num_tokens >= 1 && tokens[0].beg >= beg) {
	AV* av = newAV();
	int i;
	av_extend(av, num_tokens*2);
	for (i = 0; i < num_tokens; i++) {
	  if (tokens[i].beg) {
	    av_push(av, newSViv(tokens[i].beg-beg));
	    av_push(av, newSViv(tokens[i].end-tokens[i].beg));
	  }
	  else { /* boolean tag value */
	    av_push(av, newSViv(0));
	    av_push(av, newSViv(0));
	  }
	}
	arg = sv_2mortal(newRV_noinc((SV*)av));
      }
      break;

    case '0':
      /* token0 */
      /* fall through */
    case 'n':
      /* tagname */
      if (num_tokens >= 1) {
	arg = sv_2mortal(newSVpvn(tokens[0].beg,
				  tokens[0].end - tokens[0].beg));
	if (!p_state->xml_mode && *s == 'n')
	  sv_lower(arg);
      }
      break;

    case 'a':
      /* attr hashref */
      if (event == E_START) {
	HV* hv = newHV();
	int i;
	for (i = 1; i < num_tokens; i += 2) {
	  SV* attrname = newSVpvn(tokens[i].beg,
				  tokens[i].end-tokens[i].beg);
	  SV* attrval;

	  if (tokens[i+1].beg) {
	    char *beg = tokens[i+1].beg;
	    STRLEN len = tokens[i+1].end - beg;
	    if (*beg == '"' || *beg == '\'') {
	      assert(len < 2 || *beg != beg[len-1]);
	      beg++; len -= 2;
	    }
	    attrval = newSVpvn(beg, len);
	    decode_entities(attrval, entity2char);
	  }
	  else { /* boolean */
	    if (p_state->bool_attr_val)
	      attrval = newSVsv(p_state->bool_attr_val);
	    else
	      attrval = newSVsv(attrname);
	  }

	  if (!p_state->xml_mode)
	    sv_lower(attrname);
	  if (!hv_store_ent(hv, attrname, attrval, 0)) {
	    SvREFCNT_dec(attrval);
	  }
	  SvREFCNT_dec(attrname);
	}
	arg = sv_2mortal(newRV_noinc((SV*)hv));
      }
      break;
      
    case 'A':
      /* attrseq arrayref (v2 compatibility stuff) */
      if (event == E_START) {
	AV* av = newAV();
	int i;
	for (i = 1; i < num_tokens; i += 2) {
	  SV* attrname = newSVpvn(tokens[i].beg,
				  tokens[i].end-tokens[i].beg);
	  if (!p_state->xml_mode)
	    sv_lower(attrname);
	  av_push(av, attrname);
	}
	arg = sv_2mortal(newRV_noinc((SV*)av));
      }
      break;
	
    case 'd':
      /* origtext, data */
      arg = sv_2mortal(newSVpvn(beg, end - beg));
      break;

    case 'D':
      /* decoded text */
      if (event == E_TEXT) {
	arg = sv_2mortal(newSVpvn(beg, end - beg));
	if (!CDATA_MODE(p_state))
	  decode_entities(arg, entity2char);
      }
      break;
      
    case 'c':
      /* cdata flag */
      if (event == E_TEXT) {
	arg = boolSV(CDATA_MODE(p_state));
      }
      break;

    case '=':
      arg = sv_2mortal(newSViv(p_state->chunk_offset + offset));
      break;

    case 'E':
      /* event */
      assert(event >= 0 && event < EVENT_COUNT);
      arg = sv_2mortal(newSVpv(event_id_str[event], 0));
      break;

    case 'L':
      /* literal */
      {
	int len = s[1];
	arg = sv_2mortal(newSVpvn(s+2, len));
	s += len + 1;
      }
      break;
      
    default:
      arg = sv_2mortal(newSVpvn(s, 1));
      break;
    }

    if (!arg)
      arg = &PL_sv_undef;

    if (array) {
      /* have to fix mortality here or
	 add mortality to XPUSHs after removing it from the switch cases */
      av_push(array, SvREFCNT_inc(arg));
    }
    else {
      XPUSHs(arg);
    }
  }

  if (array) {
    av_push((AV*)h->cb, newRV_noinc((SV*)array));
  }
  else {
    PUTBACK;

    if (*attrspec == 's' && !SvROK(h->cb)) {
      char *method = SvPV(h->cb, my_na);
      perl_call_method(method, G_DISCARD | G_VOID);
    }
    else {
      perl_call_sv(h->cb, G_DISCARD | G_VOID);
    }

    FREETMPS;
    LEAVE;
  }
}


EXTERN SV*
attrspec_compile(SV* src)
{
  SV* attrspec = newSVpvn("", 0);
  STRLEN len;
  char *s = SvPV(src, len);
  char *end = s + len;

  static HV* names = 0;
  if (!names) {
    /* printf("Init attrspec names\n"); */
    names = newHV();
    hv_store(names, "self", 4,          newSVpvn("s", 1), 0);
    hv_store(names, "tokens", 6,        newSVpvn("t", 1), 0);
    hv_store(names, "token0", 6,        newSVpvn("0", 1), 0);
    hv_store(names, "tokenpos", 8,      newSVpvn("#", 1), 0);
    hv_store(names, "tagname", 7,       newSVpvn("n", 1), 0);
    hv_store(names, "attr", 4,          newSVpvn("a", 1), 0);
    hv_store(names, "attrseq", 7,       newSVpvn("A", 1), 0);
    hv_store(names, "text", 4,          newSVpvn("d", 1), 0);
    hv_store(names, "dtext", 5,         newSVpvn("D", 1), 0);
    hv_store(names, "cdata_flag", 10,   newSVpvn("c", 1), 0);
    hv_store(names, "event", 5,         newSVpvn("E", 1), 0);
    hv_store(names, "offset", 6,        newSVpvn("=", 1), 0);
  }

  while (isHSPACE(*s))
    s++;
  while (s < end) {
    if (isHNAME_FIRST(*s)) {
      char *name = s;
      SV** svp;
      s++;
      while (isHNAME_CHAR(*s))
	s++;

      /* check identifier */
      svp = hv_fetch(names, name, s - name, 0);
      if (svp) {
	sv_catsv(attrspec, *svp);
      }
      else {
	*s = '\0';
	croak("Unrecognized identifier %s in attrspec", name);
      }
    }
    else if (*s == '"' || *s == '\'') {
      char *string_beg = s;
      s++;
      while (s < end && *s != *string_beg)
	s++;
      if (*s == *string_beg) {
	/* literal */
	int len = s - string_beg - 1;
	if (len > 255)
	  croak("Can't have literal strings longer than 255 chars in attrspec");
	sv_catpvf(attrspec, "L%c", len);
	sv_catpvn(attrspec, string_beg+1, len);
	s++;
      }
      else {
	croak("Unterminated literal string in attrspec");
      }
    }
    else {
      croak("Bad attrspec (%s)", s);
    }

    while (isHSPACE(*s))
      s++;
    if (s == end)
      break;
    if (*s != ',') {
      croak("Missing comma separator in attrspec");
    }
    s++;
    while (isHSPACE(*s))
      s++;
  }
  return attrspec;
}


static char*
parse_comment(PSTATE* p_state, char *beg, char *end, STRLEN offset, SV* self)
{
  char *s = beg;

  if (p_state->strict_comment) {
    dTOKENS(4);
    char *start_com = s;  /* also used to signal inside/outside */

    while (1) {
      /* try to locate "--" */
    FIND_DASH_DASH:
      /* printf("find_dash_dash: [%s]\n", s); */
      while (s < end && *s != '-' && *s != '>')
	s++;

      if (s == end) {
	FREE_TOKENS;
	return beg;
      }

      if (*s == '>') {
	s++;
	if (start_com)
	  goto FIND_DASH_DASH;

	/* we are done recognizing all comments, make callbacks */
	report_event(p_state, E_COMMENT,
		    beg - 4, s,
		    tokens, num_tokens,
		    offset, self);
	FREE_TOKENS;

	return s;
      }

      s++;
      if (s == end) {
	FREE_TOKENS;
	return beg;
      }

      if (*s == '-') {
	/* two dashes in a row seen */
	s++;
	/* do something */
	if (start_com) {
	  PUSH_TOKEN(start_com, s-2);
	  start_com = 0;
	}
	else {
	  start_com = s;
	}
      }
    }
  }

  else { /* non-strict comment */
    token_pos_t token_pos;
    token_pos.beg = beg;
    /* try to locate /--\s*>/ which signals end-of-comment */
  LOCATE_END:
    while (s < end && *s != '-')
      s++;
    token_pos.end = s;
    if (s < end) {
      s++;
      if (*s == '-') {
	s++;
	while (isHSPACE(*s))
	  s++;
	if (*s == '>') {
	  s++;
	  /* yup */
	  report_event(p_state, E_COMMENT, beg-4, s, &token_pos, 1,
		       offset, self);
	  return s;
	}
      }
      if (s < end) {
	s = token_pos.end + 2;
	goto LOCATE_END;
      }
    }
    
    if (s == end)
      return beg;
  }

  return 0;
}


#ifdef MARKED_SECTION

static void
marked_section_update(PSTATE* p_state)
{
  /* we look at p_state->ms_stack to determine p_state->ms */
  AV* ms_stack = p_state->ms_stack;
  p_state->ms = MS_NONE;

  if (ms_stack) {
    int i;
    int stack_len = av_len(ms_stack);
    int stack_idx;
    for (stack_idx = 0; stack_idx <= stack_len; stack_idx++) {
      SV** svp = av_fetch(ms_stack, stack_idx, 0);
      if (svp) {
	AV* tokens = (AV*)SvRV(*svp);
	int tokens_len = av_len(tokens);
	int i;
	assert(SvTYPE(tokens) == SVt_PVAV);
	for (i = 0; i <= tokens_len; i++) {
	  SV** svp = av_fetch(tokens, i, 0);
	  if (svp) {
	    STRLEN len;
	    char *token_str = SvPV(*svp, len);
	    enum marked_section_t token;
	    if (strEQ(token_str, "include"))
	      token = MS_INCLUDE;
	    else if (strEQ(token_str, "rcdata"))
	      token = MS_RCDATA;
	    else if (strEQ(token_str, "cdata"))
	      token = MS_CDATA;
	    else if (strEQ(token_str, "ignore"))
	      token = MS_IGNORE;
	    else
	      token = MS_NONE;
	    if (p_state->ms < token)
	      p_state->ms = token;
	  }
	}
      }
    }
  }
  /* printf("MS %d\n", p_state->ms); */
  return;
}


static char*
parse_marked_section(PSTATE* p_state, char *beg, char *end, SV* self)
{
  char *s = beg;
  AV* tokens = 0;

  if (!p_state->marked_sections)
    return 0;

 FIND_NAMES:
  while (isHSPACE(*s))
    s++;
  while (isHNAME_FIRST(*s)) {
    char *name_start = s;
    char *name_end;
    s++;
    while (isHNAME_CHAR(*s))
      s++;
    name_end = s;
    while (isHSPACE(*s))
      s++;
    if (s == end)
      goto PREMATURE;

    if (!tokens)
      tokens = newAV();
    av_push(tokens, sv_lower(newSVpvn(name_start, name_end - name_start)));
  }
  if (*s == '-') {
    s++;
    if (*s == '-') {
      /* comment */
      s++;
      while (1) {
	while (s < end && *s != '-')
	  s++;
	if (s == end)
	  goto PREMATURE;

	s++;  /* skip first '-' */
	if (*s == '-') {
	  s++;
	  /* comment finished */
	  goto FIND_NAMES;
	}
      }      
    }
    else
      goto FAIL;
      
  }
  if (*s == '[') {
    s++;
    /* yup */

    if (!tokens) {
      tokens = newAV();
      av_push(tokens, newSVpvn("include", 7));
    }

    if (!p_state->ms_stack)
      p_state->ms_stack = newAV();
    av_push(p_state->ms_stack, newRV_noinc((SV*)tokens));
    marked_section_update(p_state);
    return s;
  }

 FAIL:
  SvREFCNT_dec(tokens);
  return 0; /* not yet implemented */
  
 PREMATURE:
  SvREFCNT_dec(tokens);
  return beg;
}
#endif


static char*
parse_decl(PSTATE* p_state, char *beg, char *end, STRLEN offset, SV* self)
{
  char *s = beg + 2;

  if (*s == '-') {
    /* comment? */

    char *tmp;
    s++;
    if (s == end)
      return beg;

    if (*s != '-')
      return 0;  /* nope, illegal */

    /* yes, two dashes seen */
    s++;

    tmp = parse_comment(p_state, s, end, offset, self);
    return (tmp == s) ? beg : tmp;
  }

#ifdef MARKED_SECTION
  if (*s == '[') {
    /* marked section */
    char *tmp;
    s++;
    tmp = parse_marked_section(p_state, s, end, self);
    return (tmp == s) ? beg : tmp;
  }
#endif

  if (*s == '>') {
    /* make <!> into empty comment <SGML Handbook 36:32> */
    token_pos_t empty;
    empty.beg = s;
    empty.end = s;
    s++;
    report_event(p_state, E_COMMENT, beg, s, &empty, 1, offset, self);
    return s;
  }

  if (isALPHA(*s)) {
    dTOKENS(8);

    s++;
    /* declaration */
    while (s < end && isHNAME_CHAR(*s))
      s++;
    /* first word available */
    PUSH_TOKEN(beg+2, s);

    while (s < end && isHSPACE(*s)) {
      s++;
      while (s < end && isHSPACE(*s))
	s++;

      if (s == end)
	goto PREMATURE;

      if (*s == '"' || *s == '\'') {
	char *str_beg = s;
	s++;
	while (s < end && *s != *str_beg)
	  s++;
	if (s == end)
	  goto PREMATURE;
	s++;
	PUSH_TOKEN(str_beg, s);
      }
      else if (*s == '-') {
	/* comment */
	char *com_beg = s;
	s++;
	if (s == end)
	  goto PREMATURE;
	if (*s != '-')
	  goto FAIL;
	s++;

	while (1) {
	  while (s < end && *s != '-')
	    s++;
	  if (s == end)
	    goto PREMATURE;
	  s++;
	  if (s == end)
	    goto PREMATURE;
	  if (*s == '-') {
	    s++;
	    PUSH_TOKEN(com_beg, s);
	    break;
	  }
	}
      }
      else if (*s != '>') {
	/* plain word */
	char *word_beg = s;
	s++;
	while (s < end && isHNOT_SPACE_GT(*s))
	  s++;
	if (s == end)
	  goto PREMATURE;
	PUSH_TOKEN(word_beg, s);
      }
      else {
	break;
      }
    }

    if (s == end)
      goto PREMATURE;
    if (*s == '>') {
      s++;
      report_event(p_state, E_DECLARATION, beg, s, tokens, num_tokens,
		   offset, self);
      FREE_TOKENS;
      return s;
    }

  FAIL:
    FREE_TOKENS;
    return 0;

  PREMATURE:
    FREE_TOKENS;
    return beg;

  }
  return 0;
}


static char*
parse_start(PSTATE* p_state, char *beg, char *end, STRLEN offset, SV* self)
{
  char *s = beg;
  SV* attr;
  int empty_tag = 0;  /* XML feature */
  dTOKENS(16);

  hctype_t tag_name_first, tag_name_char;
  hctype_t attr_name_first, attr_name_char;

  if (p_state->strict_names || p_state->xml_mode) {
    tag_name_first = attr_name_first = HCTYPE_NAME_FIRST;
    tag_name_char  = attr_name_char  = HCTYPE_NAME_CHAR;
  }
  else {
    tag_name_first = tag_name_char = HCTYPE_NOT_SPACE_GT;
    attr_name_first = HCTYPE_NOT_SPACE_GT;
    attr_name_char  = HCTYPE_NOT_SPACE_EQ_GT;
  }


  assert(beg[0] == '<' && isHNAME_FIRST(beg[1]) && end - beg > 2);
  s += 2;

  while (s < end && isHCTYPE(*s, tag_name_char))
    s++;
  PUSH_TOKEN(beg+1, s);  /* tagname */

  while (isHSPACE(*s))
    s++;
  if (s == end)
    goto PREMATURE;

  while (isHCTYPE(*s, attr_name_first)) {
    /* attribute */
    char *attr_name_beg = s;
    char *attr_name_end;
    s++;
    while (s < end && isHCTYPE(*s, attr_name_char))
      s++;
    if (s == end)
      goto PREMATURE;

    attr_name_end = s;
    PUSH_TOKEN(attr_name_beg, attr_name_end); /* attr name */

    while (isHSPACE(*s))
      s++;
    if (s == end)
      goto PREMATURE;

    if (*s == '=') {
      /* with a value */
      s++;
      while (isHSPACE(*s))
	s++;
      if (s == end)
	goto PREMATURE;
      if (*s == '>') {
	/* parse it similar to ="" */
	PUSH_TOKEN(s, s);
	break;
      }
      if (*s == '"' || *s == '\'') {
	char *str_beg = s;
	s++;
	while (s < end && *s != *str_beg)
	  s++;
	if (s == end)
	  goto PREMATURE;
	s++;
	PUSH_TOKEN(str_beg, s);
      }
      else {
	char *word_start = s;
	while (s < end && isHNOT_SPACE_GT(*s)) {
	  if (p_state->xml_mode && *s == '/')
	    break;
	  s++;
	}
	if (s == end)
	  goto PREMATURE;
	PUSH_TOKEN(word_start, s);
      }
      while (isHSPACE(*s))
	s++;
      if (s == end)
	goto PREMATURE;
    }
    else {
      PUSH_TOKEN(0, 0); /* boolean attr value */
    }
  }

  if (p_state->xml_mode && *s == '/') {
    s++;
    if (s == end)
      goto PREMATURE;
    empty_tag = 1;
  }

  if (*s == '>') {
    s++;
    /* done */
    report_event(p_state, E_START, beg, s, tokens, num_tokens, offset, self);
    if (empty_tag)
      report_event(p_state, E_END, s, s, tokens, 1,
		   offset + (s - beg), self);
    FREE_TOKENS;

    if (1) {
      /* find out if this start tag should put us into literal_mode
       */
      int i;
      int tag_len = tokens[0].end - tokens[0].beg;

      for (i = 0; literal_mode_elem[i].len; i++) {
	if (tag_len == literal_mode_elem[i].len) {
	  /* try to match it */
	  char *s = beg + 1;
	  char *t = literal_mode_elem[i].str;
	  int len = tag_len;
	  while (len) {
	    if (toLOWER(*s) != *t)
	      break;
	    s++;
	    t++;
	    if (!--len) {
	      /* found it */
	      p_state->literal_mode = literal_mode_elem[i].str;
	      /* printf("Found %s\n", p_state->literal_mode); */
	      goto END_OF_LITERAL_SEARCH;
	    }
	  }
	}
      }
    END_OF_LITERAL_SEARCH:
    }

    return s;
  }
  
  FREE_TOKENS;
  return 0;

 PREMATURE:
  FREE_TOKENS;
  return beg;
}


static char*
parse_end(PSTATE* p_state, char *beg, char *end, STRLEN offset, SV* self)
{
  char *s = beg+2;
  hctype_t name_first, name_char;

  if (p_state->strict_names) {
    name_first = HCTYPE_NAME_FIRST;
    name_char  = HCTYPE_NAME_CHAR;
  }
  else {
    name_first = name_char = HCTYPE_NOT_SPACE_GT;
  }

  if (isHCTYPE(*s, name_first)) {
    token_pos_t tagname;
    tagname.beg = s;
    s++;
    while (s < end && isHCTYPE(*s, name_char))
      s++;
    tagname.end = s;
    while (isHSPACE(*s))
      s++;
    if (s < end) {
      if (*s == '>') {
	s++;
	/* a complete end tag has been recognized */
	report_event(p_state, E_END, beg, s, &tagname, 1, offset, self);
	return s;
      }
    }
    else {
      return beg;
    }
  }
  return 0;
}


static char*
parse_process(PSTATE* p_state, char *beg, char *end,
	      STRLEN offset, SV* self)
{
  char *s = beg + 2;
  /* processing instruction */
  token_pos_t token_pos;
  token_pos.beg = s;

 FIND_PI_END:
  while (s < end && *s != '>')
    s++;
  if (*s == '>') {
    token_pos.end = s;
    s++;

    if (p_state->xml_mode) {
      /* XML processing instructions are ended by "?>" */
      if (s - beg < 4 || s[-2] != '?')
	goto FIND_PI_END;
      token_pos.end = s - 2;
    }

    /* a complete processing instruction seen */
    report_event(p_state, E_PROCESS, beg, s, &token_pos, 1, offset, self);
    return s;
  }
  else {
    return beg;
  }
  return 0;
}


static char*
parse_null(PSTATE* p_state, char *beg, char *end, STRLEN offset, SV* self)
{
  return 0;
}



#include "pfunc.h"                   /* declares the parsefunc[] */

EXTERN void
parse(PSTATE* p_state,
	   SV* chunk,
	   SV* self)
{
  char *s, *t, *beg, *end, *new_pos;
  STRLEN len;

  if (!chunk || !SvOK(chunk)) {
    /* EOF */
    if (p_state->buf && SvOK(p_state->buf)) {
      /* flush it */
      STRLEN len;
      char *s = SvPV(p_state->buf, len);
      assert(len);
      report_event(p_state, E_TEXT, s, s+len, 0, 0, 0, self);
      p_state->chunk_offset += len;
      SvREFCNT_dec(p_state->buf);
      p_state->buf = 0;
    }
    return;
  }

  if (p_state->buf && SvOK(p_state->buf)) {
    sv_catsv(p_state->buf, chunk);
    beg = SvPV(p_state->buf, len);
  }
  else {
    beg = SvPV(chunk, len);
  }

  if (!len)
    return; /* nothing to do */

  s = beg;
  t = beg;
  end = s + len;

  while (1) {
    /*
     * At the start of this loop we will always be ready for eating text
     * or a new tag.  We will never be inside some tag.  The 't' points
     * to where we started and the 's' is advanced as we go.
     */

    while (p_state->literal_mode) {
      char *l = p_state->literal_mode;
      char *end_text;

      while (s < end && *s != '<')
	s++;

      if (s == end) {
	s = t;
	goto DONE;
      }

      end_text = s;
      s++;
      
      /* here we rely on '\0' termination of perl svpv buffers */
      if (*s == '/') {
	s++;
	while (*l && toLOWER(*s) == *l) {
	  s++;
	  l++;
	}

	if (!*l) {
	  /* matched it all */
	  token_pos_t end_token;
	  end_token.beg = end_text + 1;
	  end_token.end = s;

	  while (isHSPACE(*s))
	    s++;
	  if (*s == '>') {
	    s++;
	    if (t != end_text)
	      report_event(p_state, E_TEXT, t, end_text, 0, 0, t - beg, self);
	    report_event(p_state, E_END,  end_text, s, &end_token, 1,
			 end_text - beg, self);
	    p_state->literal_mode = 0;
	    t = s;
	  }
	}
      }
    }

#ifdef MARKED_SECTION
    while (p_state->ms == MS_CDATA || p_state->ms == MS_RCDATA) {
      while (s < end && *s != ']')
	s++;
      if (*s == ']') {
	char *end_text = s;
	s++;
	if (*s == ']') {
	  s++;
	  if (*s == '>') {
	    s++;
	    /* marked section end */
	    if (t != end_text)
	      report_event(p_state, E_TEXT, t, end_text, 0, 0, t - beg, self);
	    t = s;
	    SvREFCNT_dec(av_pop(p_state->ms_stack));
	    marked_section_update(p_state);
	    continue;
	  }
	}
      }
      if (s == end) {
	s = t;
	goto DONE;
      }
    }
#endif

    /* first we try to match as much text as possible */
    while (s < end && *s != '<') {
#ifdef MARKED_SECTION
      if (p_state->ms && *s == ']') {
	char *end_text = s;
	s++;
	if (*s == ']') {
	  s++;
	  if (*s == '>') {
	    s++;
	    report_event(p_state, E_TEXT, t, end_text, 0, 0, t - beg, self);
	    SvREFCNT_dec(av_pop(p_state->ms_stack));
	    marked_section_update(p_state);    
	    t = s;
	    continue;
	  }
	}
      }
#endif
      s++;
    }
    if (s != t) {
      if (*s == '<') {
	report_event(p_state, E_TEXT, t, s, 0, 0, t - beg, self);
	t = s;
      }
      else {
	s--;
	if (isHSPACE(*s)) {
	  /* wait with white space at end */
	  while (s >= t && isHSPACE(*s))
	    s--;
	}
	else {
	  /* might be a chopped up entities/words */
	  while (s >= t && !isHSPACE(*s))
	    s--;
	  while (s >= t && isHSPACE(*s))
	    s--;
	}
	s++;
	if (s != t)
	  report_event(p_state, E_TEXT, t, s, 0, 0, beg - t, self);
	break;
      }
    }

    if (end - s < 3)
      break;

    /* next char is known to be '<' and pointed to by 't' as well as 's' */
    s++;

    if ( (new_pos = parsefunc[*s](p_state, t, end, t - beg, self))) {
      if (new_pos == t) {
	/* no progress, need more data to know what it is */
	s = t;
	break;
      }
      t = s = new_pos;
    }

    /* if we get out here then this was not a conforming tag, so
     * treat it is plain text at the top of the loop again (we
     * have already skipped past the "<").
     */
  }

 DONE:

  p_state->chunk_offset += (s - beg);

  if (s == end) {
    if (p_state->buf) {
      SvOK_off(p_state->buf);
    }
  }
  else {
    /* need to keep rest in buffer */
    if (p_state->buf) {
      /* chop off some chars at the beginning */
      if (SvOK(p_state->buf))
	sv_chop(p_state->buf, s);
      else
	sv_setpvn(p_state->buf, s, end - s);
    }
    else {
      p_state->buf = newSVpv(s, end - s);
    }
  }
  return;
}
