/**
   @file utils_misc.h

   A simple test driver and test cases for DSME
   <p>
   Copyright (C) 2011 Nokia Corporation

   @author Jyrki Hämäläinen <ext-jyrki.hamalainen@nokia.com>

   This file is part of Dsme.

   Dsme is free software; you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License
   version 2.1 as published by the Free Software Foundation.

   Dsme is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with Dsme.  If not, see <http://www.gnu.org/licenses/>.
*/


#ifndef DSME_TEST_MISCUTILS_H
#define DSME_TEST_MISCUTILS_H

static const char* dsme_module_path = "../modules/.libs/";

static bool message_queue_is_empty(void)
{
  int     count = 0;
  GSList* node;

  count = g_slist_length(message_queue);

  if (count == 1) {
      fprintf(stderr, "[=> 1 more message queued]\n");
  } else if (count) {
      fprintf(stderr, "[=> %d more messages queued]\n", count);
  } else {
      fprintf(stderr, "[=> no more messages]\n");
  }

  if (count != 0) {
      for (node = message_queue; node; node = node->next) {
          fprintf(stderr, "[%x]\n", ((queued_msg_t*)(node->data))->data->type_);
      }
  }

  return count == 0;
}

#define queued(T) ((T*)queued_(DSME_MSG_ID_(T), #T))
static inline void* queued_(unsigned type, const char* name)
{
  dsmemsg_generic_t* msg = 0;
  GSList*            node;
  char*              other_messages = 0;

  for (node = message_queue; node; node = node->next)
  {
      queued_msg_t* m = node->data;

      if (m->data->type_ == type) {
          msg = m->data;
          free(m);
          message_queue = g_slist_delete_link(message_queue, node);
          break;
      } else {
          if (other_messages == 0) {
              asprintf(&other_messages, "%x", m->data->type_);
          } else {
              char* s = 0;
              asprintf(&s, "%s, %x", other_messages, m->data->type_);
              free(other_messages), other_messages = s;
          }
      }
  }

  fprintf(stderr, msg ? "[=> %s was queued]\n" : "[=> %s was not queued\n", name);

  if (other_messages) {
      fprintf(stderr, "[=> other messages: %s]\n", other_messages);
      free(other_messages);
  }
  return msg;
}

#define TEST_MSG_INIT(T) DSME_MSG_INIT(T); fprintf(stderr, "\n[%s ", #T)

static inline void send_message(const module_t* module, const void* msg)
{
  endpoint_t endpoint = { module, 0 };
  fprintf(stderr, " SENT]\n");
  handle_message(&endpoint, module, msg);
}


/* UTILITY */

static void fatal(const char* format, ...)
{
  va_list ap;
  va_start(ap, format);
  fprintf(stderr, format, ap);
  fprintf(stderr, "\n");
  va_end(ap);
  exit(EXIT_FAILURE);
}

static module_t* load_module_under_test(const char* path)
{
  module_t* module = 0;
  char*     canonical;

  fprintf(stderr, "\n[LOADING MODULE %s]\n", path);

  canonical = realpath(path, 0);
  if (!canonical) {
      perror(path);
      fatal("realpath() failed");
  } else {
      if (!(module = load_module(canonical, 0))) {
          fatal("load_module() failed");
      }
      free(canonical);
  }

  return module;
}

static void unload_module_under_test(module_t* module)
{
  fprintf(stderr, "\n[UNLOADING MODULE]\n");
  if (!unload_module(module)) {
      fatal("unload_module() failed");
  }
}

#endif /* DSME_TEST_MISCUTILS_H */
