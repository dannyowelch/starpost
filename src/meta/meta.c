#define _POSIX_C_SOURCE 200809L

#include "meta.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void scores_clear(ScoreTable *t)
{
    if (!t)
        return;
    memset(t, 0, sizeof *t);
}

int scores_qualifies(const ScoreTable *t, Difficulty diff, uint32_t score)
{
    if (!t || score == 0)
        return 0;
    if (diff < 0 || diff >= DIFF_COUNT)
        return 0;
    if (t->count[diff] < SCORE_CAP)
        return 1;
    return score >= t->rows[diff][SCORE_CAP - 1].score;
}

void scores_insert(ScoreTable *t, Difficulty diff, const char initials[SCORE_INITIALS],
                   uint32_t score)
{
    int i;
    int n;
    ScoreRow row;
    if (!t || !initials || diff < 0 || diff >= DIFF_COUNT)
        return;
    if (!scores_qualifies(t, diff, score))
        return;
    memset(&row, 0, sizeof row);
    for (i = 0; i < SCORE_INITIALS; i++) {
        char c = initials[i];
        if (c < 'A' || c > 'Z')
            c = 'A';
        row.initials[i] = c;
    }
    row.score = score;
    n = t->count[diff];
    i = 0;
    while (i < n && t->rows[diff][i].score > score)
        i++;
    if (i >= SCORE_CAP)
        return;
    if (n < SCORE_CAP)
        n++;
    for (int j = n - 1; j > i; j--)
        t->rows[diff][j] = t->rows[diff][j - 1];
    t->rows[diff][i] = row;
    t->count[diff] = n;
}

static int mkdir_parent(const char *file)
{
    char dir[512];
    size_t len;
    char *p;
    if (!file)
        return -1;
    len = strlen(file);
    if (len == 0 || len >= sizeof dir)
        return -1;
    memcpy(dir, file, len + 1);
    p = strrchr(dir, '/');
    if (!p)
        return 0;
    *p = '\0';
    if (dir[0] == '\0')
        return 0;
    for (p = dir + 1; *p; p++) {
        if (*p != '/')
            continue;
        *p = '\0';
        if (mkdir(dir, 0755) != 0 && errno != EEXIST)
            return -1;
        *p = '/';
    }
    if (mkdir(dir, 0755) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

int scores_default_path(char *buf, int cap)
{
    const char *xdg;
    const char *home;
    int n;
    if (!buf || cap < 32)
        return -1;
    xdg = getenv("XDG_DATA_HOME");
    home = getenv("HOME");
    if (xdg && xdg[0])
        n = snprintf(buf, (size_t)cap, "%s/starpost-returns/scores.txt", xdg);
    else if (home && home[0])
        n = snprintf(buf, (size_t)cap, "%s/.local/share/starpost-returns/scores.txt", home);
    else
        return -1;
    if (n < 0 || n >= cap)
        return -1;
    return 0;
}

int scores_save(const ScoreTable *t, const char *path)
{
    char tmp[540];
    FILE *f;
    int d;
    int n;
    if (!t || !path || !path[0])
        return -1;
    n = snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof tmp)
        return -1;
    if (mkdir_parent(path) != 0)
        return -1;
    f = fopen(tmp, "w");
    if (!f)
        return -1;
    if (fprintf(f, "STARPOST-SCORES 1\n") < 0) {
        fclose(f);
        return -1;
    }
    for (d = 0; d < DIFF_COUNT; d++) {
        int i;
        int count = t->count[d];
        if (count < 0)
            count = 0;
        if (count > SCORE_CAP)
            count = SCORE_CAP;
        if (fprintf(f, "%s %d\n", diff_name((Difficulty)d), count) < 0) {
            fclose(f);
            return -1;
        }
        for (i = 0; i < count; i++) {
            const ScoreRow *row = &t->rows[d][i];
            if (fprintf(f, "%c%c%c %u\n", row->initials[0], row->initials[1], row->initials[2],
                        row->score) < 0) {
                fclose(f);
                return -1;
            }
        }
    }
    if (fflush(f) != 0) {
        fclose(f);
        return -1;
    }
    if (fsync(fileno(f)) != 0) {
        fclose(f);
        return -1;
    }
    if (fclose(f) != 0)
        return -1;
    if (rename(tmp, path) != 0)
        return -1;
    return 0;
}

static int parse_initials(const char *s, char out[SCORE_INITIALS])
{
    if (!s || strlen(s) != SCORE_INITIALS)
        return 0;
    for (int i = 0; i < SCORE_INITIALS; i++) {
        if (s[i] < 'A' || s[i] > 'Z')
            return 0;
        out[i] = s[i];
    }
    return 1;
}

int scores_load(ScoreTable *t, const char *path)
{
    FILE *f;
    char line[128];
    int ver = 0;
    int d;
    if (!t)
        return 0;
    scores_clear(t);
    if (!path || !path[0])
        return 0;
    f = fopen(path, "r");
    if (!f)
        return 0;
    if (!fgets(line, sizeof line, f) || sscanf(line, "STARPOST-SCORES %d", &ver) != 1 || ver != 1) {
        fclose(f);
        scores_clear(t);
        return 0;
    }
    for (d = 0; d < DIFF_COUNT; d++) {
        char name[32];
        int count = 0;
        int i;
        int stop = 0;
        if (!fgets(line, sizeof line, f))
            break;
        if (sscanf(line, "%31s %d", name, &count) != 2 || strcmp(name, diff_name((Difficulty)d)) != 0 ||
            count < 0 || count > SCORE_CAP) {
            break;
        }
        for (i = 0; i < count; i++) {
            char initials[8];
            unsigned score = 0;
            char letters[SCORE_INITIALS];
            if (!fgets(line, sizeof line, f) ||
                sscanf(line, "%7s %u", initials, &score) != 2 || !parse_initials(initials, letters)) {
                count = i;
                stop = 1;
                break;
            }
            memcpy(t->rows[d][i].initials, letters, SCORE_INITIALS);
            t->rows[d][i].initials[SCORE_INITIALS] = '\0';
            t->rows[d][i].score = score;
        }
        t->count[d] = count;
        if (stop)
            break;
    }
    fclose(f);
    return 0;
}

static int edge(bool now, bool *prev)
{
    int fired = now && !*prev;
    *prev = now;
    return fired;
}

static char cycle_letter(char c, int dir)
{
    int n = c - 'A';
    if (n < 0 || n > 25)
        n = 0;
    n = (n + dir) % 26;
    if (n < 0)
        n += 26;
    return (char)('A' + n);
}

void session_init(Session *s, uint32_t seed)
{
    if (!s)
        return;
    memset(s, 0, sizeof *s);
    if (seed == 0)
        seed = 1;
    s->screen = SCREEN_TITLE;
    s->difficulty = DIFF_ADVANCED;
    s->seed = seed;
    s->initials[0] = s->initials[1] = s->initials[2] = 'A';
}

static void latch_initials_input(Session *s, const SessionInput *in)
{
    s->prev_up = in->up;
    s->prev_down = in->down;
    s->prev_left = in->play.left;
    s->prev_right = in->play.right;
    s->prev_accept = in->accept;
    s->prev_pause = in->pause;
}

static void enter_initials(Session *s)
{
    s->pending_score = s->game.score;
    s->initials[0] = s->initials[1] = s->initials[2] = 'A';
    s->initial_slot = 0;
    s->initials_wait = STARPOST_INITIALS_DELAY;
    s->screen = SCREEN_INITIALS;
}

static void finish_game(Session *s)
{
    if (scores_qualifies(&s->scores, s->difficulty, s->game.score))
        enter_initials(s);
    else
        s->screen = SCREEN_TITLE;
}

int session_update(Session *s, const SessionInput *in, float dt)
{
    int up;
    int down;
    int left;
    int right;
    int accept;
    int pause;
    if (!s || !in)
        return 0;
    if (dt < 0.0f)
        dt = 0.0f;
    up = edge(in->up, &s->prev_up);
    down = edge(in->down, &s->prev_down);
    left = edge(in->play.left, &s->prev_left);
    right = edge(in->play.right, &s->prev_right);
    accept = edge(in->accept, &s->prev_accept);
    pause = edge(in->pause, &s->prev_pause);

    switch (s->screen) {
    case SCREEN_TITLE:
        s->attract += dt;
        if (up)
            s->title_item = (s->title_item + TITLE_COUNT - 1) % TITLE_COUNT;
        if (down)
            s->title_item = (s->title_item + 1) % TITLE_COUNT;
        if (accept) {
            if (s->title_item == TITLE_START) {
                uint32_t next = s->seed * 1664525u + 1013904223u;
                game_init_difficulty(&s->game, s->seed, s->difficulty);
                s->seed = next == 0 ? 1 : next;
                s->screen = SCREEN_PLAY;
            } else if (s->title_item == TITLE_DIFFICULTY) {
                s->diff_item = (int)s->difficulty;
                s->screen = SCREEN_DIFFICULTY;
            } else if (s->title_item == TITLE_SCORES) {
                s->score_diff = (int)s->difficulty;
                s->screen = SCREEN_SCORES;
            } else {
                s->quit = 1;
            }
        }
        return 0;
    case SCREEN_DIFFICULTY:
        if (up || left)
            s->diff_item = (s->diff_item + DIFF_COUNT - 1) % DIFF_COUNT;
        if (down || right)
            s->diff_item = (s->diff_item + 1) % DIFF_COUNT;
        if (accept) {
            s->difficulty = (Difficulty)s->diff_item;
            s->screen = SCREEN_TITLE;
        } else if (pause) {
            s->screen = SCREEN_TITLE;
        }
        return 0;
    case SCREEN_SCORES:
        if (left)
            s->score_diff = (s->score_diff + DIFF_COUNT - 1) % DIFF_COUNT;
        if (right)
            s->score_diff = (s->score_diff + 1) % DIFF_COUNT;
        if (accept || pause)
            s->screen = SCREEN_TITLE;
        return 0;
    case SCREEN_PLAY:
        if (pause) {
            s->pause_item = PAUSE_RESUME;
            s->screen = SCREEN_PAUSE;
            return 0;
        }
        game_update(&s->game, &in->play, dt);
        if (s->game.state == STATE_GAME_OVER)
            finish_game(s);
        return 1;
    case SCREEN_PAUSE:
        if (pause) {
            s->screen = SCREEN_PLAY;
            return 0;
        }
        if (up)
            s->pause_item = (s->pause_item + PAUSE_COUNT - 1) % PAUSE_COUNT;
        if (down)
            s->pause_item = (s->pause_item + 1) % PAUSE_COUNT;
        if (accept) {
            if (s->pause_item == PAUSE_RESUME)
                s->screen = SCREEN_PLAY;
            else
                s->screen = SCREEN_TITLE;
        }
        return 0;
    case SCREEN_INITIALS:
        if (s->initials_wait > 0.0f) {
            s->initials_wait -= dt;
            if (s->initials_wait < 0.0f)
                s->initials_wait = 0.0f;
            latch_initials_input(s, in);
            return 0;
        }
        if (s->initial_slot < SCORE_INITIALS) {
            if (up)
                s->initials[s->initial_slot] = cycle_letter(s->initials[s->initial_slot], 1);
            if (down)
                s->initials[s->initial_slot] = cycle_letter(s->initials[s->initial_slot], -1);
        }
        if (left)
            s->initial_slot = (s->initial_slot + INITIALS_COLUMNS - 1) % INITIALS_COLUMNS;
        if (right)
            s->initial_slot = (s->initial_slot + 1) % INITIALS_COLUMNS;
        if (accept || pause) {
            if (s->initial_slot < SCORE_INITIALS) {
                s->initial_slot++;
            } else {
                scores_insert(&s->scores, s->difficulty, s->initials, s->pending_score);
                s->scores_dirty = 1;
                s->screen = SCREEN_TITLE;
            }
        }
        return 0;
    default:
        return 0;
    }
}
