/* $Id: Parser.xs,v 2.79 1999/12/08 17:27:36 gisle Exp $
 *
 * Copyright 1999, Gisle Aas.
 * Copyright 1999, Michael A. Chase.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the same terms as Perl itself.
 */


/*
 * Standard XS greeting.
 */
#ifdef __cplusplus
extern "C" {
#endif
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#ifdef __cplusplus
}
#endif



/*
 * Some perl version compatibility gruff.
 */
#include "patchlevel.h"
#if PATCHLEVEL <= 4 /* perl5.004_XX */

#ifndef PL_sv_undef
   #define PL_sv_undef sv_undef
   #define PL_sv_yes   sv_yes
#endif

#ifndef PL_hexdigit
   #define PL_hexdigit hexdigit
#endif

#if (PATCHLEVEL == 4 && SUBVERSION <= 4)
/* The newSVpvn function was introduced in perl5.004_05 */
static SV *
newSVpvn(char *s, STRLEN len)
{
    register SV *sv = newSV(0);
    sv_setpvn(sv,s,len);
    return sv;
}
#endif /* not perl5.004_05 */
#endif /* perl5.004_XX */



/*
 * Include stuff.  We include .c files instead of linking them,
 * so that they don't have to pollute the external dll name space.
 */

#ifdef EXTERN
  #undef EXTERN
#endif

#define EXTERN static /* Don't pollute */

EXTERN
HV* entity2char;            /* %HTML::Entities::entity2char */

#include "hparser.h"
#include "util.c"
#include "hparser.c"


/*
 * Support functions for the XS glue
 */

static SV*
check_handler(SV* h)
{
  if (SvROK(h)) {
    SV* ref = SvRV(h);
    if (SvTYPE(ref) == SVt_PVCV)
      return newSVsv(h);
    if (SvTYPE(ref) == SVt_PVAV)
      return SvREFCNT_inc(ref);
    croak("Only code or array references allowed as handler");
  }
  return SvOK(h) ? newSVsv(h) : 0;
}


static PSTATE*
get_pstate(SV* sv)                               /* used by XS typemap */
{
  HV* hv;
  SV** svp;

  sv = SvRV(sv);
  if (!sv || SvTYPE(sv) != SVt_PVHV)
    croak("Not a reference to a hash");
  hv = (HV*)sv;
  svp = hv_fetch(hv, "_parser_xs_state", 16, 0);
  if (svp) {
    PSTATE* p = (PSTATE*)SvIV(*svp);
#ifdef P_MAGIC
    if (p->magic != P_MAGIC)
      croak("Bad magic in parser state object at %p", p);
#endif
    return p;
  }
  croak("Can't find '_parser_xs_state' element in HTML::Parser hash");
  return 0;
}



/*
 *  XS interface definition.
 */

MODULE = HTML::Parser		PACKAGE = HTML::Parser

PROTOTYPES: DISABLE

void
_alloc_pstate(self)
	SV* self;
    PREINIT:
	PSTATE* pstate;
	SV* sv;
	HV* hv;
    CODE:
	sv = SvRV(self);
        if (!sv || SvTYPE(sv) != SVt_PVHV)
            croak("Self is not a reference to a hash");
	hv = (HV*)sv;

	Newz(56, pstate, 1, PSTATE);
#ifdef P_MAGIC
	pstate->magic = P_MAGIC;
#endif
	sv = newSViv((IV)pstate);
	SvREADONLY_on(sv);

	hv_store(hv, "_parser_xs_state", 16, sv, 0);

void
DESTROY(pstate)
	PSTATE* pstate
    PREINIT:
        int i;
    CODE:
	SvREFCNT_dec(pstate->buf);
#ifdef MARKED_SECTION
        SvREFCNT_dec(pstate->ms_stack);
#endif
        SvREFCNT_dec(pstate->bool_attr_val);
        for (i = 0; i < EVENT_COUNT; i++) {
          SvREFCNT_dec(pstate->handlers[i].cb);
          SvREFCNT_dec(pstate->handlers[i].argspec);
        }

	Safefree(pstate);


void
parse(self, chunk)
	SV* self;
	SV* chunk
    PREINIT:
	PSTATE* pstate = get_pstate(self);
    PPCODE:
	parse(pstate, chunk, self);
	XSRETURN(1); /* self */

SV*
strict_comment(pstate,...)
	PSTATE* pstate
    ALIAS:
	HTML::Parser::strict_comment = 1
	HTML::Parser::strict_names = 2
        HTML::Parser::xml_mode = 3
	HTML::Parser::unbroken_text = 4
        HTML::Parser::marked_sections = 5
    PREINIT:
	bool *attr;
    CODE:
        switch (ix) {
	case  1: attr = &pstate->strict_comment;       break;
	case  2: attr = &pstate->strict_names;         break;
	case  3: attr = &pstate->xml_mode;             break;
	case  4: attr = &pstate->unbroken_text;        break;
        case  5:
#ifdef MARKED_SECTION
		 attr = &pstate->marked_sections;      break;
#else
	         croak("marked sections not supported"); break;
#endif
	default:
	    croak("Unknown boolean attribute (%d)", ix);
        }
	RETVAL = boolSV(*attr);
	if (items > 1)
	    *attr = SvTRUE(ST(1));
    OUTPUT:
	RETVAL

SV*
boolean_attribute_value(pstate,...)
        PSTATE* pstate
    CODE:
	RETVAL = pstate->bool_attr_val ? newSVsv(pstate->bool_attr_val)
				       : &PL_sv_undef;
	if (items > 1) {
	    SvREFCNT_dec(pstate->bool_attr_val);
	    pstate->bool_attr_val = newSVsv(ST(1));
        }
    OUTPUT:
	RETVAL

SV*
handler(pstate, eventname,...)
	PSTATE* pstate
	SV* eventname
    PREINIT:
	SV* self = ST(0);
	STRLEN name_len;
	char *name = SvPV(eventname, name_len);
        int event = -1;
        int i;
        struct p_handler *h;
    PPCODE:
	/* map event name string to event_id */
	for (i = 0; i < EVENT_COUNT; i++) {
	  if (strEQ(name, event_id_str[i])) {
	    event = i;
	    break;
	  }
	}
        if (event < 0)
	    croak("No handler for %s events", name);

	h = &pstate->handlers[event];

	/* set up return value */
	if (h->cb) {
	  ST(0) = (SvTYPE(h->cb) == SVt_PVAV)
	             ? sv_2mortal(newRV_inc(h->cb))
	             : sv_2mortal(newSVsv(h->cb));
	}
        else {
	  ST(0) = &PL_sv_undef;
        }

        /* update */
        if (items > 3) {
	    SvREFCNT_dec(h->argspec);
	    h->argspec = 0;
	    h->argspec = argspec_compile(ST(3));
	}
        if (items > 2) {
	    SvREFCNT_dec(h->cb);
            h->cb = 0;
	    h->cb = check_handler(ST(2));
	}

        XSRETURN(1);


MODULE = HTML::Parser		PACKAGE = HTML::Entities

void
decode_entities(...)
    PREINIT:
        int i;
    PPCODE:
	if (GIMME_V == G_SCALAR && items > 1)
            items = 1;
	for (i = 0; i < items; i++) {
	    if (GIMME_V != G_VOID)
	        ST(i) = sv_2mortal(newSVsv(ST(i)));
	    else if (SvREADONLY(ST(i)))
		croak("Can't inline decode readonly string");
	    decode_entities(ST(i), entity2char);
	}
        XSRETURN(items);


MODULE = HTML::Parser		PACKAGE = HTML::Parser

BOOT:
    entity2char = perl_get_hv("HTML::Entities::entity2char", TRUE);
