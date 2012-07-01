/*
  Copyright (c) 2009 Dave Gamble
  Copyright (c) 2011 Jonathan Reams

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

/* cJSON */
/* JSON parser in C. */

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <float.h>
#include <limits.h>
#include <ctype.h>
#include "cJSON.h"

static const char *ep;

const char *cJSON_GetErrorPtr() {return ep;}

#ifndef HAVE_STRCASECMP
static int cJSON_strcasecmp(const char *s1,const char *s2)
{
	if (!s1) return (s1==s2)?0:1;if (!s2) return 1;
	for(; tolower(*s1) == tolower(*s2); ++s1, ++s2)	if(*s1 == 0)	return 0;
	return tolower(*(const unsigned char *)s1) - tolower(*(const unsigned char *)s2);
}
#else
#define cJSON_strcasecmp(a, b) strcasecmp(a, b)
#endif

static void *(*cJSON_malloc)(size_t sz) = malloc;
static void (*cJSON_free)(void *ptr) = free;

static char* cJSON_strdup(const char* str)
{
      size_t len;
      char* copy;

      len = strlen(str) + 1;
      if (!(copy = (char*)cJSON_malloc(len))) return 0;
      memcpy(copy,str,len);
      return copy;
}

void cJSON_InitHooks(cJSON_Hooks* hooks)
{
    if (!hooks) { /* Reset hooks */
        cJSON_malloc = malloc;
        cJSON_free = free;
        return;
    }

	cJSON_malloc = (hooks->malloc_fn)?hooks->malloc_fn:malloc;
	cJSON_free	 = (hooks->free_fn)?hooks->free_fn:free;
}

/* Objects and Arrays are implemented as red-black trees.
   These routines implement a generic red-black tree. */
inline void set_parent(cJSON * parent, cJSON * node) {
	node->parent = parent;
}
inline void set_child(cJSON * child, cJSON * node, int isleft) {
	if(isleft)
		node->left = child;
	else
		node->right = child;
}
inline cJSON * child_first(cJSON * node) {
	while(node->left)
		node = node->left;
	return node;
}
inline cJSON * child_last(cJSON * node) {
	while(node->right)
		node = node->right;
	return node;
}
inline cJSON * obj_first(cJSON * obj) {
	if(obj->type != cJSON_Object && obj->type != cJSON_Array)
		return NULL;
	return obj->first;
}
inline cJSON * obj_last(cJSON * obj) {
	if(obj->type != cJSON_Object && obj->type != cJSON_Array)
		return NULL;
	return obj->last;
}
inline cJSON * child_next(cJSON * node) {
	cJSON * parent;
	if(node->right)
		return child_first(node->right);
	while((parent = node->parent) && parent->right == node)
		node = parent;
	return parent;
}
inline cJSON * child_prev(cJSON * node) {
	cJSON * parent;
	if(node->left)
		return child_last(node->left);
	while((parent = node->parent) && parent->left == node)
		node = parent;
	return parent;
}

inline cJSON * tree_lookup(const char * key, int index,
	cJSON * tree, cJSON ** parent, int *isleft) {
	if(tree->type != cJSON_Object && tree->type != cJSON_Array)
		return NULL;
	cJSON * object = tree->root;
	if(parent)
		*parent = NULL;
	if(isleft)
		*isleft = 0;
	while(object) {
		int res = 0;
		if(tree->type == cJSON_Object)
			res = cJSON_strcasecmp(key, object->string);
		else if(object->index > index)
			res = 1;
		else if(object->index < index)
			res = -1;
		if(res == 0)
			return object;
		if(parent)
			*parent = object;
		if(res > 0) {
			if(isleft)
				*isleft = 1;
			object = object->left;
		} else {
			if(isleft)
				*isleft = 0;
			object = object->right;
		}
	}
	return NULL;
}

void tree_rotate_left(cJSON * node, cJSON * tree) {
	cJSON * p = node;
	cJSON * q = node->right;
	cJSON * parent = p->parent;
	if(parent != NULL) {
		if(parent->left == p)
			parent->left = q;
		else
			parent->right = q;
	} else
		tree->root = q;
	set_parent(parent, q);
	set_parent(q, p);

	p->right = q->left;
	if(p->right)
		set_parent(p, p->right);
	q->left = p;
}

void tree_rotate_right(cJSON * node, cJSON * tree) {
	cJSON * p = node;
	cJSON * q = node->left;
	cJSON * parent = p->parent;

	if(parent != NULL) {
		if(parent->left == p)
			parent->left = q;
		else
			parent->right = q;
	} else
		tree->root = q;
	set_parent(parent, q);
	set_parent(q, p);

	p->left = q->right;
	if(p->left)
		set_parent(p, p->left);
	q->right = p;
}

cJSON * tree_insert_item(cJSON * node, cJSON * tree) {
	cJSON *item, *parent;
	int isleft;
	if(tree->type != cJSON_Object && tree->type != cJSON_Array)
		return NULL;

	item = tree_lookup(node->string, node->index,
		tree, &parent, &isleft);
	if(item)
		return item;

	node->left = NULL;
	node->right = NULL;
	node->isred = 1;
	set_parent(parent, node);

	if(parent) {
		if(isleft && parent == tree->first)
			tree->first = node;
		else if(parent == tree->last)
			tree->last = node;
		set_child(node, parent, isleft);
	} else {
		tree->root = node;
		tree->first = node;
		tree->last = node;
	}

	while((parent = node->parent) && parent->isred & 1) {
		cJSON * grandpa = parent->parent;
		if(parent == grandpa->left) {
			cJSON * uncle = grandpa->right;

			if(uncle && uncle->isred & 1) {
				parent->isred = 0;
				uncle->isred = 0;
				grandpa->isred = 1;
				node = grandpa;
			} else {
				if(node == parent->right) {
					tree_rotate_left(parent, tree);
					node = parent;
					parent = node->parent;
				}
				parent->isred = 0;
				grandpa->isred = 1;
				tree_rotate_right(grandpa, tree);
			}
		} else {
			cJSON * uncle = grandpa->left;

			if(uncle && uncle->isred & 1) {
				parent->isred = 0;
				uncle->isred = 0;
				grandpa->isred = 1;
				node = grandpa;
			} else {
				if(node == parent->left) {
					tree_rotate_right(parent, tree);
					node = parent;
					parent = node->parent;
				}
				parent->isred = 0;
				grandpa->isred = 1;
				tree_rotate_left(grandpa, tree);
			}
		}
	}
	tree->root->isred = 0;
	return NULL;
}

void tree_remove(cJSON * node, cJSON * tree) {
	if(tree->type != cJSON_Object && tree->type != cJSON_Array)
		return ;
	cJSON * parent = node->parent;
	cJSON * left = node->left;
	cJSON * right = node->right;
	cJSON * next;
	int color;

	if(node == tree->first)
		tree->first = child_next(node);
	if(node == tree->last)
		tree->last = child_next(node);

	if(!left)
		next = right;
	else if(!right)
		next = left;
	else
		next = child_first(right);

	if(parent)
		set_child(next, parent, parent->left == node);
	else
		tree->root = next;

	if(left && right) {
		color = next->isred;
		next->isred = node->isred;
		next->left = left;
		set_parent(next, left);

		if(next != right) {
			parent = next->parent;
			set_parent(node->parent, next);

			node = next->right;
			parent->left = node;

			next->right = right;
			set_parent(next, right);
		} else {
			set_parent(parent, next);
			parent = next;
			node = next->right;
		}
	} else {
		color = node->isred;
		node = next;
	}

	if(node)
		set_parent(parent, node);

	if(color & 1)
		return;
	if(node && node->isred & 1) {
		node->isred = 0;
		return;
	}

	do {
		if(node == tree->root)
			break;
		if(node == parent->left) {
			cJSON * sibling = parent->right;
			if(sibling->isred & 1) {
				sibling->isred = 0;
				parent->isred = 1;
				tree_rotate_left(parent, tree);
				sibling = parent->right;
			}
			if((!sibling->left || sibling->left->isred == 0) &&
				(!sibling->right || sibling->right->isred == 0)) {
				sibling->isred = 1;
				node = parent;
				parent = parent->parent;
				continue;
			}
			if(!sibling->right || sibling->right->isred == 0) {
				sibling->left->isred = 0;
				sibling->isred = 1;
				tree_rotate_right(sibling, tree);
				sibling = parent->right;
			}
			sibling->isred = parent->isred;
			parent->isred = 0;
			sibling->right->isred = 0;
			tree_rotate_left(parent, tree);
			node = tree->root;
			break;
		} else {
			cJSON * sibling = parent->right;
			if(sibling->isred & 1) {
				sibling->isred = 0;
				parent->isred = 1;
				tree_rotate_right(parent, tree);
				sibling = parent->left;
			}
			if((!sibling->left || sibling->left->isred == 0) &&
				(!sibling->right || sibling->right->isred == 0)) {
				sibling->isred = 1;
				node = parent;
				parent = parent->parent;
				continue;
			}
			if(!sibling->left || sibling->left->isred == 0) {
				sibling->right->isred = 0;
				sibling->isred = 1;
				tree_rotate_left(sibling, tree);
				sibling = parent->left;
			}
			sibling->isred = parent->isred;
			parent->isred = 0;
			sibling->left->isred = 0;
			tree_rotate_right(parent, tree);
			node = tree->root;
			break;
		}
	} while(node->isred == 0);
	if(node)
		node->isred = 0;
}

/* Internal constructor. */
static cJSON *cJSON_New_Item()
{
	cJSON* node = (cJSON*)cJSON_malloc(sizeof(cJSON));
	if (node) memset(node,0,sizeof(cJSON));
	return node;
}

/* Delete a cJSON structure. */
void cJSON_Delete(cJSON *c)
{
	if(c->root)
		cJSON_Delete(c->root);
	if(c->left)
		cJSON_Delete(c->left);
	if(c->right)
		cJSON_Delete(c->right);
	if(c->string)
		cJSON_free(c->string);
	if(!(c->type&cJSON_IsReference) && c->valuestring)
		cJSON_free(c->valuestring);
	cJSON_free(c);
}

/* Parse the input text to generate a number, and populate the result into item. */
static const char *parse_number(cJSON *item,const char *num)
{
	double n=0,sign=1,scale=0;int subscale=0,signsubscale=1;

	/* Could use sscanf for this? */
	if (*num=='-') sign=-1,num++;	/* Has sign? */
	if (*num=='0') num++;			/* is zero */
	if (*num>='1' && *num<='9')	do	n=(n*10.0)+(*num++ -'0');	while (*num>='0' && *num<='9');	/* Number? */
	if (*num=='.' && num[1]>='0' && num[1]<='9') {num++;		do	n=(n*10.0)+(*num++ -'0'),scale--; while (*num>='0' && *num<='9');}	/* Fractional part? */
	if (*num=='e' || *num=='E')		/* Exponent? */
	{	num++;if (*num=='+') num++;	else if (*num=='-') signsubscale=-1,num++;		/* With sign? */
		while (*num>='0' && *num<='9') subscale=(subscale*10)+(*num++ - '0');	/* Number? */
	}

	n=sign*n*pow(10.0,(scale+subscale*signsubscale));	/* number = +/- number.fraction * 10^+/- exponent */
	
	item->valuedouble=n;
	item->valueint=(int)n;
	item->type=cJSON_Number;
	return num;
}

/* Render the number nicely from the given item into a string. */
static char *print_number(cJSON *item, size_t * outlen)
{
	char *str;
	double d=item->valuedouble;
	size_t len;
	if (fabs(((double)item->valueint)-d)<=DBL_EPSILON && d<=INT_MAX && d>=INT_MIN)
	{
		str=(char*)cJSON_malloc(21);	/* 2^64+1 can be represented in 21 chars. */
		if (str) len =sprintf(str,"%d",item->valueint);
	}
	else
	{
		str=(char*)cJSON_malloc(64);	/* This is a nice tradeoff. */
		if (str)
		{
			if (fabs(floor(d)-d)<=DBL_EPSILON)
				len = sprintf(str,"%.0f",d);
			else if (fabs(d)<1.0e-6 || fabs(d)>1.0e9)
				len = sprintf(str,"%e",d);
			else
				len =sprintf(str,"%f",d);
		}
	}
	if(outlen)
		*outlen += len;
	return str;
}

/* Parse the input text into an unescaped cstring, and populate item. */
static const unsigned char firstByteMark[7] = { 0x00, 0x00, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC };
static const char *parse_string(cJSON *item,const char *str)
{
	const char *ptr=str+1;
	char *ptr2, *out;
	int len=0;
	unsigned uc,uc2;
	if (*str!='\"') {ep=str;return 0;}	/* not a string! */
	
	while (*ptr!='\"' && *ptr && ++len) if (*ptr++ == '\\') ptr++;	/* Skip escaped quotes. */
	
	out=(char*)cJSON_malloc(len+1);	/* This is how long we need for the string, roughly. */
	if (!out) return 0;
	
	ptr=str+1;ptr2=out;
	while (*ptr!='\"' && *ptr)
	{
		if (*ptr!='\\') *ptr2++=*ptr++;
		else
		{
			ptr++;
			switch (*ptr)
			{
				case 'b': *ptr2++='\b';	break;
				case 'f': *ptr2++='\f';	break;
				case 'n': *ptr2++='\n';	break;
				case 'r': *ptr2++='\r';	break;
				case 't': *ptr2++='\t';	break;
				case 'u':	 /* transcode utf16 to utf8. */
					sscanf(ptr+1,"%4x",&uc);ptr+=4;	/* get the unicode char. */

					if ((uc>=0xDC00 && uc<=0xDFFF) || uc==0)	break;	// check for invalid.

					if (uc>=0xD800 && uc<=0xDBFF)	// UTF16 surrogate pairs.
					{
						if (ptr[1]!='\\' || ptr[2]!='u')	break;	// missing second-half of surrogate.
						sscanf(ptr+3,"%4x",&uc2);ptr+=6;
						if (uc2<0xDC00 || uc2>0xDFFF)		break;	// invalid second-half of surrogate.
						uc=0x10000 | ((uc&0x3FF)<<10) | (uc2&0x3FF);
					}

					len=4;if (uc<0x80) len=1;else if (uc<0x800) len=2;else if (uc<0x10000) len=3; ptr2+=len;
					
					switch (len) {
						case 4: *--ptr2 =((uc | 0x80) & 0xBF); uc >>= 6;
						case 3: *--ptr2 =((uc | 0x80) & 0xBF); uc >>= 6;
						case 2: *--ptr2 =((uc | 0x80) & 0xBF); uc >>= 6;
						case 1: *--ptr2 =(uc | firstByteMark[len]);
					}
					ptr2+=len;
					break;
				default:  *ptr2++=*ptr; break;
			}
			ptr++;
		}
	}
	*ptr2=0;
	if (*ptr=='\"') ptr++;
	item->valuestring=out;
	item->vstrlen = ptr2 - out;
	item->type=cJSON_String;
	return ptr;
}

/* Render the cstring provided to an escaped version that can be printed. */
static char *print_string_ptr(const char *str, size_t * outlen)
{
	const char *ptr;char *ptr2,*out;int len=0;unsigned char token;
	
	if (!str) return cJSON_strdup("");
	ptr=str;while ((token=*ptr) && ++len) {if (strchr("\"\\\b\f\n\r\t",token)) len++; else if (token<32) len+=5;ptr++;}
	
	out=(char*)cJSON_malloc(len+3);
	if (!out) return 0;

	ptr2=out;ptr=str;
	*ptr2++='\"';
	while (*ptr)
	{
		if ((unsigned char)*ptr>31 && *ptr!='\"' && *ptr!='\\') *ptr2++=*ptr++;
		else
		{
			*ptr2++='\\';
			switch (token=*ptr++)
			{
				case '\\':	*ptr2++='\\';	break;
				case '\"':	*ptr2++='\"';	break;
				case '\b':	*ptr2++='b';	break;
				case '\f':	*ptr2++='f';	break;
				case '\n':	*ptr2++='n';	break;
				case '\r':	*ptr2++='r';	break;
				case '\t':	*ptr2++='t';	break;
				default: sprintf(ptr2,"u%04x",token);ptr2+=5;	break;	/* escape and print */
			}
		}
	}
	*ptr2++='\"';*ptr2++=0;
	if(outlen)
		*outlen += len + 3;
	return out;
}
/* Invote print_string_ptr (which is useful) on an item. */
static char *print_string(cJSON *item, size_t *outlen)	{return print_string_ptr(item->valuestring, outlen);}

/* Predeclare these prototypes. */
static const char *parse_value(cJSON *item,const char *value);
static char *print_value(cJSON *item,int depth,int fmt, size_t *outlen);
static const char *parse_array(cJSON *item,const char *value);
static char *print_array(cJSON *item,int depth,int fmt, size_t *outlen);
static const char *parse_object(cJSON *item,const char *value);
static char *print_object(cJSON *item,int depth,int fmt, size_t * outlen);

/* Utility to jump whitespace and cr/lf */
static const char *skip(const char *in) {while (in && *in && (unsigned char)*in<=32) in++; return in;}

/* Parse an object - create a new root, and populate. */
cJSON *cJSON_Parse(const char *value)
{
	cJSON *c=cJSON_New_Item();
	ep=0;
	if (!c) return 0;       /* memory fail */

	if (!parse_value(c,skip(value))) {cJSON_Delete(c);return 0;}
	return c;
}

cJSON *cJSON_ParseFile(const char* filename)
{
  cJSON *cJSON;
  FILE *fp;
  long len;
  char *buf;
  fp = fopen(filename, "rb");
  if (fp) {
    fseek(fp,0,SEEK_END); //go to end
    len=ftell(fp); //get position at end (length)
    fseek(fp,0,SEEK_SET); //go to beg.
    buf=(char *)malloc(len); //malloc buffer
    fread(buf,len,1,fp); //read into buffer
    fclose(fp);
    cJSON = cJSON_Parse(buf);
    free(buf);
    return cJSON;
  }

  return NULL;
}

/* Render a cJSON item/entity/structure to text. */
char *cJSON_Print(cJSON *item)				{return print_value(item,0,1,NULL);}
char *cJSON_PrintUnformatted(cJSON *item)	{return print_value(item,0,0,NULL);}

/* Parser core - when encountering text, process appropriately. */
static const char *parse_value(cJSON *item,const char *value)
{
	if (value == NULL)				return 0;	/* Fail on null. */
	if (!strncmp(value,"null",4))	{ item->type=cJSON_NULL;  return value+4; }
	if (!strncmp(value,"false",5))	{ item->type=cJSON_False; return value+5; }
	if (!strncmp(value,"true",4))	{ item->type=cJSON_True; item->valueint=1;	return value+4; }
	if (*value=='\"')				{ return parse_string(item,value); }
	if (*value=='-' || (*value>='0' && *value<='9'))	{ return parse_number(item,value); }
	if (*value=='[')				{ return parse_array(item,value); }
	if (*value=='{')				{ return parse_object(item,value); }

	ep=value;
	return NULL;	/* failure. */
}

/* Render a value to text. */
static char *print_value(cJSON *item,int depth,int fmt, size_t *outlen)
{
	char *out=0;
	if (!item) return 0;
	switch ((item->type)&255)
	{
		case cJSON_NULL:
			out=cJSON_strdup("null"); 
			if(outlen)
				*outlen +=5;
			break;
		case cJSON_False:
			out=cJSON_strdup("false");
			if(outlen)
				*outlen += 6;
			break;
		case cJSON_True:
			out=cJSON_strdup("true");
			if(outlen)
				*outlen += 5;
			break;
		case cJSON_Number:	out=print_number(item, outlen);break;
		case cJSON_String:	out=print_string(item, outlen);break;
		case cJSON_Array:	out=print_array(item,depth,fmt, outlen);break;
		case cJSON_Object:	out=print_object(item,depth,fmt, outlen);break;
	}
	return out;
}

/* Build an array from input text. */
static const char *parse_array(cJSON *item,const char *value)
{
	if (*value!='[')	{ep=value;return 0;}	/* not an array! */

	item->type=cJSON_Array;
	value=skip(value+1);
	if (*value==']') return value+1;	/* empty array. */

	do {
		cJSON * child = cJSON_New_Item();
		if(!child)
			return NULL;
		if(*value == ',')
			value++;
		value = skip(parse_value(child, skip(value)));
		if(!value)
			return NULL;
		child->index = item->count++;
		tree_insert_item(child, item);
	} while(value && *value==',');
	if(*value == ']')
		return value + 1;
	ep = value;
	return NULL;
}

/* Render an array to text */
static char *print_array(cJSON *item,int depth,int fmt, size_t *outlen)
{
	char **entries;
	char *out=0,*ptr,*ret;
	cJSON * child = obj_first(item);
	int i=0,fail=0;
	size_t locallen = 5;
	
	/* Allocate an array to hold the values for each */
	entries=(char**)cJSON_malloc(item->count*sizeof(char*));
	if (!entries) return 0;
	/* Retrieve all the results: */
	while (child && !fail)
	{
		ret=print_value(child,depth+1,fmt, &locallen);
		entries[i++]=ret;
		if (ret) locallen+=2+(fmt?1:0);
		else fail=1;
		child = child_next(child);
	}
	
	/* If we didn't fail, try to malloc the output string */
	if (!fail) out=(char*)cJSON_malloc(locallen);
	/* If that fails, we fail. */
	if (!out) fail=1;

	/* Handle failure. */
	if (fail)
	{
		for (i=0;i<item->count;i++) {
			if (entries[i])
				cJSON_free(entries[i]);
		}
		cJSON_free(entries);
		return 0;
	}
	
	/* Compose the output array. */
	*out='[';
	ptr=out+1;*ptr=0;
	for (i=0;i<item->count;i++)
	{
		strcpy(ptr,entries[i]);ptr+=strlen(entries[i]);
		if (i!=item->count-1) {
			*ptr++=',';
			if(fmt) *ptr++=' ';
			*ptr=0;
		}
		cJSON_free(entries[i]);
	}
	cJSON_free(entries);
	*ptr++=']';*ptr++=0;
	if(outlen)
		*outlen += locallen;
	return out;	
}

/* Build an object from the text. */
static const char *parse_object(cJSON *item,const char *value)
{
	cJSON * child;
	if (*value!='{')	{ep=value;return 0;}	/* not an object! */
	
	item->type=cJSON_Object;
	value=skip(value+1);
	if (*value=='}') return value+1;	/* empty object. */
	
	do {
		child = cJSON_New_Item();
		if(!child)
			return NULL;
		if(*value == ',')
			value++;
		value = skip(parse_string(child, skip(value)));
		if(!value)
			return NULL;
		child->string = child->valuestring;
		if(*value != ':') {
			ep = value;
			return NULL;
		}
		value = skip(parse_value(child,skip(value + 1)));
		if(!value)
			return NULL;
		if(child->type != cJSON_String)
			child->valuestring = NULL;
		tree_insert_item(child, item);
		item->count++;
	} while(value && *value==',');
	
	if (*value=='}') return value+1;	/* end of array */
	ep=value;return NULL;	/* malformed. */
}

/* Render an object to text. */
static char *print_object(cJSON *item,int depth,int fmt, size_t *outlen)
{
	char **entries=NULL,**names=NULL;
	char *out=NULL,*ptr,*ret,*str;
	size_t len=7;
	int i=0,j,fail=0;
	cJSON *child = obj_first(item);
	/* Allocate space for the names and the objects */
	entries = (char**)cJSON_malloc(item->count*sizeof(char*));
	if (!entries)
		return NULL;
	names = (char**)cJSON_malloc(item->count*sizeof(char*));
	if (!names) {
		cJSON_free(entries);
		return NULL;
	}

	/* Collect all the results into our arrays: */
	depth++;
	if(fmt)
		len+=depth;
	while(child)
	{
		names[i]=str=print_string_ptr(child->string, &len);
		entries[i++]=ret=print_value(child,depth,fmt, &len);
		if (str && ret) len+=2+(fmt?2+depth:0);
		else fail=1;
		child= child_next(child);
	}
	
	/* Try to allocate the output string */
	if (!fail) out=(char*)cJSON_malloc(len);
	if (!out) fail=1;

	/* Handle failure */
	if (fail)
	{
		for (i=0;i<item->count;i++) {
			if (names[i]) cJSON_free(names[i]);
			if (entries[i]) cJSON_free(entries[i]);
		}
		cJSON_free(names);
		cJSON_free(entries);
		return NULL;
	}
	
	/* Compose the output: */
	*out='{';
	ptr=out+1;
	if (fmt)*ptr++='\n';
	for (i=0;i<item->count;i++)
	{
		if (fmt) {
			for (j=0;j<depth;j++) 
				*ptr++='\t';
		}
		strcpy(ptr,names[i]);
		ptr+=strlen(names[i]);
		*ptr++=':';
		if (fmt) *ptr++='\t';
		strcpy(ptr,entries[i]);
		ptr+=strlen(entries[i]);
		if (i!=item->count - 1) *ptr++=',';
		if (fmt) *ptr++='\n';
		cJSON_free(names[i]);
		cJSON_free(entries[i]);
	}
	
	cJSON_free(names);
	cJSON_free(entries);
	if (fmt) {
		for (i=0;i<depth-1;i++)
			*ptr++='\t';
	}
	*ptr++='}';
	*ptr++=0;
	if(outlen)
		*outlen += len;
	return out;	
}

/* Get Array size/item / object item. */
int cJSON_GetArraySize(cJSON *array) {
	return array->count;
}

cJSON *cJSON_GetArrayItem(cJSON *array,int item) {
	return tree_lookup(NULL, item, array, NULL, NULL);
}

cJSON *cJSON_GetObjectItem(cJSON *object,const char *string) {
	return tree_lookup(string, 0, object, NULL, NULL);
}

cJSON *cJSON_GetFirstChild(cJSON *object) {
	return obj_first(object);
}
cJSON *cJSON_GetNextChild(cJSON *object) {
	return child_next(object);
}


/* Utility for handling references. */
static cJSON *create_reference(cJSON *item) {
	cJSON *ref=cJSON_New_Item();
	if (!ref) return NULL;
	memcpy(ref,item,sizeof(cJSON));
	ref->string=NULL;
	ref->type|=cJSON_IsReference;
	return ref;
}

/* Add item to array/object. */
void cJSON_AddItemToArray(cJSON * array, cJSON * item) {
	if(array->type != cJSON_Array)
		return;
	if(!item)
		return;
	item->index = array->count++;
	tree_insert_item(item, array);
}

void cJSON_AddItemToObject(cJSON * object, const char * string, cJSON * item) {
	if(object->type != cJSON_Object)
		return;
	if(!item)
		return;
	if(item->string)
		cJSON_free(item->string);
	item->string = cJSON_strdup(string);
	object->count++;
	tree_insert_item(item, object);
}
void cJSON_AddItemReferenceToArray(cJSON *array, cJSON *item)						{cJSON_AddItemToArray(array,create_reference(item));}
void cJSON_AddItemReferenceToObject(cJSON *object,const char *string,cJSON *item)	{cJSON_AddItemToObject(object,string,create_reference(item));}

cJSON * cJSON_DetachItemFromArray(cJSON * array, int which) {
	cJSON * parent;
	int isleft;
	cJSON * item = tree_lookup(NULL, which, array, &parent, &isleft); 
	if(!item)
		return NULL;
	tree_remove(item, array);
	return item;
}

void cJSON_DeleteItemFromArray(cJSON *array,int which) {cJSON_Delete(cJSON_DetachItemFromArray(array,which));}

cJSON *cJSON_DetachItemFromObject(cJSON *object,const char *string) {
	cJSON * parent;
	int isleft;
	cJSON * item = tree_lookup(string, 0, object, &parent, &isleft); 
	if(!item)
		return NULL;
	tree_remove(item, object);
	object->count--;
	return item;
}
void cJSON_DeleteItemFromObject(cJSON *object,const char *string) {cJSON_Delete(cJSON_DetachItemFromObject(object,string));}

void tree_replace(cJSON * obj, int index, const char * string, cJSON * newitem) {
	cJSON * item, *parent;
	int isleft;

	item = tree_lookup(string, index, obj, &parent, &isleft);
	if(!item)
		return;

	if(parent)
		set_child(parent, newitem, isleft);
	if(item->left)
		set_parent(newitem, item->left);
	if(item->right)
		set_parent(newitem, item->right);

	if(obj->first == item)
		obj->first = newitem;
	if(obj->last == item)
		obj->last = newitem;
	cJSON_Delete(item);
}

/* Replace array/object items with new ones. */
void   cJSON_ReplaceItemInArray(cJSON *array,int which,cJSON *newitem)
{
	tree_replace(array, which, NULL, newitem);
}
void cJSON_ReplaceItemInObject(cJSON * object, const char * string,
	cJSON * newitem)
{
	tree_replace(object, 0, string, newitem);
}
/* Create basic types: */
cJSON *cJSON_CreateNull()						{cJSON *item=cJSON_New_Item();if(item)item->type=cJSON_NULL;return item;}
cJSON *cJSON_CreateTrue()						{cJSON *item=cJSON_New_Item();if(item)item->type=cJSON_True;return item;}
cJSON *cJSON_CreateFalse()						{cJSON *item=cJSON_New_Item();if(item)item->type=cJSON_False;return item;}
cJSON *cJSON_CreateBool(int b)					{cJSON *item=cJSON_New_Item();if(item)item->type=b?cJSON_True:cJSON_False;return item;}
cJSON *cJSON_CreateNumber(double num)			{cJSON *item=cJSON_New_Item();if(item){item->type=cJSON_Number;item->valuedouble=num;item->valueint=(int)num;}return item;}
cJSON *cJSON_CreateString(const char *string)	{cJSON *item=cJSON_New_Item();if(item){item->type=cJSON_String;item->valuestring=cJSON_strdup(string);}return item;}
cJSON *cJSON_CreateArray()						{cJSON *item=cJSON_New_Item();if(item)item->type=cJSON_Array;return item;}
cJSON *cJSON_CreateObject()						{cJSON *item=cJSON_New_Item();if(item)item->type=cJSON_Object;return item;}

/* Create Arrays: */
cJSON *cJSON_CreateIntArray(int *numbers,int count)	{
	int i;
	cJSON *n=NULL,*a=cJSON_CreateArray();
	if(!a)
		return NULL;
	for(i=0; i<count; i++) {
		n=cJSON_CreateNumber(numbers[i]);
		n->index = i;
		tree_insert_item(n, a);
	}
	a->count=count;
	return a;
}
cJSON *cJSON_CreateFloatArray(float *numbers,int count) {
	int i;
	cJSON *n=0,*a=cJSON_CreateArray();
	if(!a)
		return NULL;
	for(i=0;i<count;i++) {
		n=cJSON_CreateNumber(numbers[i]);
		n->index = i;
		tree_insert_item(n, a);
	}
	a->count=count;
	return a;
}

cJSON *cJSON_CreateDoubleArray(double *numbers,int count) {
	int i;
	cJSON *n=0,*a=cJSON_CreateArray();
	if(!a)
		return NULL;
	for(i=0;i<count;i++) {
		n=cJSON_CreateNumber(numbers[i]);
		n->index = i;
		tree_insert_item(n, a);		
	}
	a->count = count;
	return a;
}
cJSON *cJSON_CreateStringArray(const char **strings,int count) {
	int i;
	cJSON *n=0,*a=cJSON_CreateArray();
	if(!a)
		return NULL;
	for(i=0;i<count;i++) {
		n=cJSON_CreateString(strings[i]);
		n->index = i;
		tree_insert_item(n, a);
	}
	return a;
}
