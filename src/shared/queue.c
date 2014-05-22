/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2012-2014  Intel Corporation. All rights reserved.
 *
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "src/shared/util.h"
#include "src/shared/queue.h"

struct queue_entry {
	void *data;
	struct queue_entry *next;
};

struct queue {
	int ref_count;
	struct queue_entry *head;
	struct queue_entry *tail;
	unsigned int entries;
};

static struct queue *queue_ref(struct queue *queue)
{
	if (!queue)
		return NULL;

	__sync_fetch_and_add(&queue->ref_count, 1);

	return queue;
}

static void queue_unref(struct queue *queue)
{
	if (__sync_sub_and_fetch(&queue->ref_count, 1))
		return;

	free(queue);
}

struct queue *queue_new(void)
{
	struct queue *queue;

	queue = new0(struct queue, 1);
	if (!queue)
		return NULL;

	queue->head = NULL;
	queue->tail = NULL;
	queue->entries = 0;

	return queue_ref(queue);
}

void queue_destroy(struct queue *queue, queue_destroy_func_t destroy)
{
	struct queue_entry *entry;

	if (!queue)
		return;

	entry = queue->head;

	while (entry) {
		struct queue_entry *tmp = entry;

		if (destroy)
			destroy(entry->data);

		entry = entry->next;

		free(tmp);
	}

	queue_unref(queue);
}

bool queue_push_tail(struct queue *queue, void *data)
{
	struct queue_entry *entry;

	if (!queue)
		return false;

	entry = new0(struct queue_entry, 1);
	if (!entry)
		return false;

	entry->data = data;
	entry->next = NULL;

	if (queue->tail)
		queue->tail->next = entry;

	queue->tail = entry;

	if (!queue->head)
		queue->head = entry;

	queue->entries++;

	return true;
}

bool queue_push_head(struct queue *queue, void *data)
{
	struct queue_entry *entry;

	if (!queue)
		return false;

	entry = new0(struct queue_entry, 1);
	if (!entry)
		return false;

	entry->data = data;
	entry->next = queue->head;

	queue->head = entry;

	if (!queue->tail)
		queue->tail = entry;

	queue->entries++;

	return true;
}

void *queue_pop_head(struct queue *queue)
{
	struct queue_entry *entry;
	void *data;

	if (!queue || !queue->head)
		return NULL;

	entry = queue->head;

	if (!queue->head->next) {
		queue->head = NULL;
		queue->tail = NULL;
	} else
		queue->head = queue->head->next;

	data = entry->data;

	free(entry);
	queue->entries--;

	return data;
}

void *queue_peek_head(struct queue *queue)
{
	if (!queue || !queue->head)
		return NULL;

	return queue->head->data;
}

void *queue_peek_tail(struct queue *queue)
{
	if (!queue || !queue->tail)
		return NULL;

	return queue->tail->data;
}

static bool queue_find_entry(struct queue *queue, const void *data)
{
	struct queue_entry *entry;

	for (entry = queue->head; entry; entry = entry->next)
		if (entry == data)
			return true;

	return false;
}

void queue_foreach(struct queue *queue, queue_foreach_func_t function,
							void *user_data)
{
	struct queue_entry *entry;

	if (!queue || !function)
		return;

	entry = queue->head;
	if (!entry)
		return;

	queue_ref(queue);
	while (entry && queue->ref_count > 1) {
		struct queue_entry *tmp = entry;

		entry = tmp->next;

		function(tmp->data, user_data);

		if (!queue_find_entry(queue, entry))
			break;
	}
	queue_unref(queue);
}

static bool direct_match(const void *a, const void *b)
{
	return a == b;
}

void *queue_find(struct queue *queue, queue_match_func_t function,
							const void *match_data)
{
	struct queue_entry *entry;

	if (!queue || !function)
		return NULL;

	if (!function)
		function = direct_match;

	for (entry = queue->head; entry; entry = entry->next)
		if (function(entry->data, match_data))
			return entry->data;

	return NULL;
}

bool queue_remove(struct queue *queue, void *data)
{
	struct queue_entry *entry, *prev;

	if (!queue || !data)
		return false;

	for (entry = queue->head, prev = NULL; entry;
					prev = entry, entry = entry->next) {
		if (entry->data != data)
			continue;

		if (prev)
			prev->next = entry->next;
		else
			queue->head = entry->next;

		if (!entry->next)
			queue->tail = prev;

		free(entry);
		queue->entries--;

		return true;
	}

	return false;
}

void *queue_remove_if(struct queue *queue, queue_match_func_t function,
							void *user_data)
{
	struct queue_entry *entry, *prev = NULL;

	if (!queue || !function)
		return NULL;

	entry = queue->head;

	while (entry) {
		if (function(entry->data, user_data)) {
			void *data;

			if (prev)
				prev->next = entry->next;
			else
				queue->head = entry->next;

			if (!entry->next)
				queue->tail = prev;

			data = entry->data;

			free(entry);
			queue->entries--;

			return data;
		} else {
			prev = entry;
			entry = entry->next;
		}
	}

	return NULL;
}

unsigned int queue_remove_all(struct queue *queue, queue_match_func_t function,
				void *user_data, queue_destroy_func_t destroy)
{
	struct queue_entry *entry;
	unsigned int count = 0;

	if (!queue)
		return 0;

	entry = queue->head;

	if (function) {
		struct queue_entry *prev = NULL;

		while (entry) {
			if (function(entry->data, user_data)) {
				struct queue_entry *tmp = entry;

				if (prev)
					prev->next = entry->next;
				else
					queue->head = entry->next;

				if (!entry->next)
					queue->tail = prev;

				entry = entry->next;

				if (destroy)
					destroy(tmp->data);

				free(tmp);
				count++;
			} else {
				prev = entry;
				entry = entry->next;
			}
		}

		queue->entries -= count;
	} else {
		while (entry) {
			struct queue_entry *tmp = entry;

			entry = entry->next;

			if (destroy)
				destroy(tmp->data);

			free(tmp);
			count++;
		}

		queue->head = NULL;
		queue->tail = NULL;
		queue->entries = 0;
	}

	return count;
}

unsigned int queue_length(struct queue *queue)
{
	if (!queue)
		return 0;

	return queue->entries;
}

bool queue_isempty(struct queue *queue)
{
	if (!queue)
		return true;

	return queue->entries == 0;
}
