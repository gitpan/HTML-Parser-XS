/* $Id: hparser.h,v 2.6 1999/12/09 19:07:33 gisle Exp $
 *
 * Copyright 1999, Gisle Aas.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the same terms as Perl itself.
 */

/*
 * Declare various structures and constants.  The main thing
 * is 'struct p_state' that contains various fields to represent
 * the state of the parser.
 */

#ifdef MARKED_SECTION

enum marked_section_t {
  MS_NONE = 0,
  MS_INCLUDE,
  MS_RCDATA,
  MS_CDATA,
  MS_IGNORE,
};

#define CDATA_MODE(p_state) ((p_state)->literal_mode || \
	 	             (p_state)->ms == MS_CDATA)

#else /* MARKED_SECTION */

#define CDATA_MODE(p_state) ((p_state)->literal_mode)

#endif /* MARKED_SECTION */


#define P_MAGIC 0x16091964  /* used to tag struct p_state for safer cast */

enum event_id {
  E_DECLARATION = 0,
  E_COMMENT,
  E_START,
  E_END,
  E_TEXT,
  E_PROCESS,
  E_DEFAULT,
  /**/
  EVENT_COUNT,
};
typedef enum event_id event_id_t;

/* must match event_id_t above */
static char* event_id_str[] = {
  "declaration",
  "comment",
  "start",
  "end",
  "text",
  "process",
  "default",
};

struct p_handler {
  SV* cb;
  SV* argspec;
};

struct p_state {
  U32 magic;
  SV* buf;
  STRLEN chunk_offset;
  bool parsing;
  bool eof;

  /* special parsing modes */
  char* literal_mode;

#ifdef MARKED_SECTION
  /* marked section support */
  enum marked_section_t ms;
  AV* ms_stack;
  bool marked_sections;
#endif

  /* various boolean configuration attributes */
  bool strict_comment;
  bool strict_names;
  bool xml_mode;
  bool unbroken_text;

  /* other configuration stuff */
  SV* bool_attr_val;
  struct p_handler handlers[EVENT_COUNT];
};
typedef struct p_state PSTATE;

