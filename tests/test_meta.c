#define _POSIX_C_SOURCE 200809L

#include "meta/meta.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails;
static int checks;

static void check(int cond, const char *expr, const char *file, int line)
{
    checks++;
    if (!cond) {
        fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expr);
        fails++;
    }
}

#define CHECK(cond) check((cond), #cond, __FILE__, __LINE__)

static SessionInput none(void)
{
    SessionInput in;
    memset(&in, 0, sizeof in);
    return in;
}

static void tap(Session *s, SessionInput in)
{
    session_update(s, &in, STARPOST_DT);
    in = none();
    session_update(s, &in, STARPOST_DT);
}

static void test_difficulty_scaling(void)
{
    GameState g;
    InputFrame idle;
    int w;

    CHECK(diff_tune(DIFF_BEGINNER)->speed_scale == 0.75f);
    CHECK(diff_tune(DIFF_BEGINNER)->count_add == -2);
    CHECK(diff_tune(DIFF_BEGINNER)->interval_scale == 1.35f);
    CHECK(diff_tune(DIFF_BEGINNER)->flyer_start == 4);
    CHECK(diff_tune(DIFF_BEGINNER)->shield_recharge == 3);
    CHECK(diff_tune(DIFF_ADVANCED)->speed_scale == 1.0f);
    CHECK(diff_tune(DIFF_ADVANCED)->count_add == 0);
    CHECK(diff_tune(DIFF_ADVANCED)->flyer_start == 2);
    CHECK(diff_tune(DIFF_ADVANCED)->shield_recharge == STARPOST_SHIELD_RECHARGE);
    CHECK(diff_tune(DIFF_HYPER)->speed_scale == 1.35f);
    CHECK(diff_tune(DIFF_HYPER)->count_add == 3);
    CHECK(diff_tune(DIFF_HYPER)->interval_scale == 0.75f);
    CHECK(diff_tune(DIFF_HYPER)->flyer_start == 1);
    CHECK(diff_tune(DIFF_HYPER)->shield_recharge == 1);
    CHECK(strcmp(diff_name(DIFF_BEGINNER), "BEGINNER") == 0);
    CHECK(strcmp(diff_name(DIFF_HYPER), "HYPER") == 0);

    for (w = 1; w <= 6; w++) {
        WaveConfig base = wave_config(w);
        WaveConfig adv = wave_config_diff(w, DIFF_ADVANCED);
        CHECK(adv.enemy_count == base.enemy_count);
        CHECK(adv.enemy_speed == base.enemy_speed);
        CHECK(adv.spawn_interval == base.spawn_interval);
        CHECK(adv.flyer_count == base.flyer_count);
    }
    {
        WaveConfig late = wave_config(100);
        WaveConfig adv = wave_config_diff(100, DIFF_ADVANCED);
        CHECK(adv.enemy_count == late.enemy_count);
        CHECK(adv.flyer_count == late.flyer_count);
    }

    {
        WaveConfig base = wave_config(1);
        WaveConfig beg = wave_config_diff(1, DIFF_BEGINNER);
        WaveConfig hyp = wave_config_diff(1, DIFF_HYPER);
        CHECK(beg.enemy_count == base.enemy_count - 2);
        CHECK(beg.enemy_speed < base.enemy_speed);
        CHECK(beg.spawn_interval > base.spawn_interval);
        CHECK(beg.flyer_count == 0);
        CHECK(hyp.enemy_count == base.enemy_count + 3);
        CHECK(hyp.enemy_speed > base.enemy_speed);
        CHECK(hyp.spawn_interval < base.spawn_interval);
        CHECK(hyp.flyer_count >= 1);
        CHECK(wave_config_diff(3, DIFF_BEGINNER).flyer_count == 0);
        CHECK(wave_config_diff(4, DIFF_BEGINNER).flyer_count >= 1);
    }

    memset(&idle, 0, sizeof idle);
    game_init_difficulty(&g, 1, DIFF_BEGINNER);
    CHECK(g.difficulty == DIFF_BEGINNER);
    CHECK(g.shield_recharge == 3);
    CHECK(g.cfg.enemy_count == wave_config(1).enemy_count - 2);
    g.shield = 4;
    g.spawned = g.cfg.enemy_count + g.cfg.flyer_count;
    g.spawn_timer = 1000.0f;
    game_update(&g, &idle, STARPOST_DT);
    CHECK(g.state == STATE_WAVE_CLEAR);
    CHECK(g.shield == 7);

    game_init_difficulty(&g, 1, DIFF_HYPER);
    CHECK(g.shield_recharge == 1);
    CHECK(g.cfg.flyer_count >= 1);
    g.shield = 4;
    g.spawned = g.cfg.enemy_count + g.cfg.flyer_count;
    g.spawn_timer = 1000.0f;
    game_update(&g, &idle, STARPOST_DT);
    CHECK(g.state == STATE_WAVE_CLEAR);
    CHECK(g.shield == 5);
}

static void test_scores_table(void)
{
    ScoreTable t;
    char letters[3];
    int i;
    scores_clear(&t);
    CHECK(!scores_qualifies(&t, DIFF_ADVANCED, 0));
    CHECK(scores_qualifies(&t, DIFF_ADVANCED, 1));
    letters[0] = 'A';
    letters[1] = 'A';
    letters[2] = 'A';
    scores_insert(&t, DIFF_ADVANCED, letters, 0);
    CHECK(t.count[DIFF_ADVANCED] == 0);

    scores_insert(&t, DIFF_ADVANCED, "AAA", 50);
    letters[0] = 'B';
    scores_insert(&t, DIFF_ADVANCED, "BBB", 50);
    CHECK(t.count[DIFF_ADVANCED] == 2);
    CHECK(t.rows[DIFF_ADVANCED][0].initials[0] == 'B');
    CHECK(t.rows[DIFF_ADVANCED][1].initials[0] == 'A');
    CHECK(t.rows[DIFF_ADVANCED][0].score == 50);

    scores_insert(&t, DIFF_ADVANCED, "CCC", 80);
    CHECK(t.rows[DIFF_ADVANCED][0].initials[0] == 'C');
    CHECK(t.rows[DIFF_ADVANCED][0].score == 80);

    scores_clear(&t);
    for (i = 0; i < SCORE_CAP; i++) {
        char name[3];
        name[0] = 'A';
        name[1] = 'A';
        name[2] = (char)('A' + i);
        scores_insert(&t, DIFF_HYPER, name, (uint32_t)(1000 - i * 10));
    }
    CHECK(t.count[DIFF_HYPER] == SCORE_CAP);
    CHECK(t.rows[DIFF_HYPER][0].score == 1000);
    CHECK(t.rows[DIFF_HYPER][SCORE_CAP - 1].score == 1000 - (SCORE_CAP - 1) * 10);
    CHECK(!scores_qualifies(&t, DIFF_HYPER, 1000 - (SCORE_CAP - 1) * 10 - 1));
    scores_insert(&t, DIFF_HYPER, "ZZZ", 1000 - (SCORE_CAP - 1) * 10 - 1);
    CHECK(t.count[DIFF_HYPER] == SCORE_CAP);
    CHECK(t.rows[DIFF_HYPER][SCORE_CAP - 1].initials[0] != 'Z');
    {
        uint32_t tie = t.rows[DIFF_HYPER][SCORE_CAP - 2].score;
        char gone = t.rows[DIFF_HYPER][SCORE_CAP - 1].initials[2];
        int still = 0;
        CHECK(scores_qualifies(&t, DIFF_HYPER, tie));
        scores_insert(&t, DIFF_HYPER, "NEW", tie);
        CHECK(t.count[DIFF_HYPER] == SCORE_CAP);
        CHECK(t.rows[DIFF_HYPER][SCORE_CAP - 2].initials[0] == 'N');
        CHECK(t.rows[DIFF_HYPER][SCORE_CAP - 2].score == tie);
        CHECK(t.rows[DIFF_HYPER][SCORE_CAP - 1].score == tie);
        for (i = 0; i < SCORE_CAP; i++) {
            if (t.rows[DIFF_HYPER][i].initials[0] == 'A' &&
                t.rows[DIFF_HYPER][i].initials[2] == gone)
                still = 1;
        }
        CHECK(!still);
    }
}

static void test_scores_file(void)
{
    ScoreTable t;
    ScoreTable loaded;
    char dir[] = "/tmp/starpost-m6-XXXXXX";
    char path[256];
    char tmp[280];
    char home_path[512];
    FILE *f;
    CHECK(mkdtemp(dir) != NULL);
    snprintf(path, sizeof path, "%s/scores.txt", dir);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);

    scores_clear(&loaded);
    CHECK(scores_load(&loaded, path) == 0);
    CHECK(loaded.count[DIFF_BEGINNER] == 0);
    CHECK(loaded.count[DIFF_ADVANCED] == 0);
    CHECK(loaded.count[DIFF_HYPER] == 0);

    f = fopen(path, "w");
    CHECK(f != NULL);
    fputs("this is not a score file\n", f);
    fclose(f);
    scores_insert(&loaded, DIFF_ADVANCED, "AAA", 5);
    CHECK(scores_load(&loaded, path) == 0);
    CHECK(loaded.count[DIFF_ADVANCED] == 0);

    f = fopen(path, "w");
    CHECK(f != NULL);
    fputs("STARPOST-SCORES 1\nBEGINNER 1\nABC 40\nnot a difficulty\n", f);
    fclose(f);
    CHECK(scores_load(&loaded, path) == 0);
    CHECK(loaded.count[DIFF_BEGINNER] == 1);
    CHECK(loaded.rows[DIFF_BEGINNER][0].score == 40);
    CHECK(loaded.rows[DIFF_BEGINNER][0].initials[0] == 'A');
    CHECK(loaded.count[DIFF_ADVANCED] == 0);
    CHECK(loaded.count[DIFF_HYPER] == 0);

    scores_clear(&t);
    scores_insert(&t, DIFF_BEGINNER, "SAM", 100);
    scores_insert(&t, DIFF_ADVANCED, "MAX", 900);
    scores_insert(&t, DIFF_ADVANCED, "ANN", 400);
    scores_insert(&t, DIFF_HYPER, "ZOE", 50);
    CHECK(scores_save(&t, path) == 0);
    CHECK(access(tmp, F_OK) != 0);
    scores_clear(&loaded);
    CHECK(scores_load(&loaded, path) == 0);
    CHECK(loaded.count[DIFF_BEGINNER] == 1);
    CHECK(loaded.rows[DIFF_BEGINNER][0].score == 100);
    CHECK(memcmp(loaded.rows[DIFF_BEGINNER][0].initials, "SAM", 3) == 0);
    CHECK(loaded.count[DIFF_ADVANCED] == 2);
    CHECK(loaded.rows[DIFF_ADVANCED][0].score == 900);
    CHECK(loaded.rows[DIFF_ADVANCED][1].score == 400);
    CHECK(loaded.count[DIFF_HYPER] == 1);
    CHECK(loaded.rows[DIFF_HYPER][0].initials[0] == 'Z');

    CHECK(scores_default_path(home_path, (int)sizeof home_path) == 0);
    CHECK(strstr(home_path, "starpost-returns/scores.txt") != NULL);

    unlink(path);
    rmdir(dir);
}

static void test_session_flow(void)
{
    Session s;
    SessionInput in;
    float dist;

    session_init(&s, 7);
    CHECK(s.screen == SCREEN_TITLE);
    CHECK(s.difficulty == DIFF_ADVANCED);
    CHECK(s.quit == 0);
    in = none();
    in.down = true;
    tap(&s, in);
    CHECK(s.screen == SCREEN_TITLE);
    CHECK(s.title_item == TITLE_DIFFICULTY);
    in = none();
    in.accept = true;
    tap(&s, in);
    CHECK(s.screen == SCREEN_DIFFICULTY);
    CHECK(s.diff_item == DIFF_ADVANCED);
    in = none();
    in.down = true;
    tap(&s, in);
    CHECK(s.diff_item == DIFF_HYPER);
    in = none();
    in.accept = true;
    tap(&s, in);
    CHECK(s.screen == SCREEN_TITLE);
    CHECK(s.difficulty == DIFF_HYPER);

    in = none();
    in.down = true;
    tap(&s, in);
    CHECK(s.title_item == TITLE_SCORES);
    in = none();
    in.accept = true;
    tap(&s, in);
    CHECK(s.screen == SCREEN_SCORES);
    CHECK(s.score_diff == DIFF_HYPER);
    in = none();
    in.play.right = true;
    tap(&s, in);
    CHECK(s.score_diff == DIFF_BEGINNER);
    in = none();
    in.pause = true;
    tap(&s, in);
    CHECK(s.screen == SCREEN_TITLE);
    CHECK(s.difficulty == DIFF_HYPER);

    in = none();
    in.down = true;
    tap(&s, in);
    CHECK(s.title_item == TITLE_QUIT);
    in = none();
    in.accept = true;
    tap(&s, in);
    CHECK(s.quit == 1);

    session_init(&s, 3);
    in = none();
    in.accept = true;
    tap(&s, in);
    CHECK(s.screen == SCREEN_PLAY);
    CHECK(s.game.difficulty == DIFF_ADVANCED);
    s.game.enemies[0].alive = true;
    s.game.enemies[0].kind = ENEMY_WALKER;
    s.game.enemies[0].lane = 1;
    s.game.enemies[0].dist = 0.80f;
    s.game.enemies[0].speed = 0.50f;
    s.game.spawn_timer = 1000.0f;
    in = none();
    in.pause = true;
    session_update(&s, &in, 0.50f);
    CHECK(s.screen == SCREEN_PAUSE);
    CHECK(s.game.enemies[0].dist == 0.80f);
    session_update(&s, &in, 0.50f);
    CHECK(s.screen == SCREEN_PAUSE);
    CHECK(s.game.enemies[0].dist == 0.80f);
    in = none();
    session_update(&s, &in, 0.10f);
    in.pause = true;
    session_update(&s, &in, 0.50f);
    CHECK(s.screen == SCREEN_PLAY);
    CHECK(s.game.enemies[0].dist == 0.80f);
    in = none();
    session_update(&s, &in, 0.40f);
    CHECK(s.screen == SCREEN_PLAY);
    CHECK(s.game.enemies[0].dist == 0.80f - 0.50f * 0.40f);

    dist = s.game.enemies[0].dist;
    in = none();
    in.pause = true;
    session_update(&s, &in, 0.20f);
    CHECK(s.screen == SCREEN_PAUSE);
    in = none();
    session_update(&s, &in, 0.10f);
    in.down = true;
    session_update(&s, &in, 0.10f);
    CHECK(s.pause_item == PAUSE_QUIT);
    CHECK(s.game.enemies[0].dist == dist);
    in = none();
    session_update(&s, &in, 0.10f);
    in.accept = true;
    session_update(&s, &in, 0.30f);
    CHECK(s.screen == SCREEN_TITLE);
    CHECK(s.game.enemies[0].dist == dist);

    session_init(&s, 9);
    in = none();
    in.accept = true;
    tap(&s, in);
    s.game.state = STATE_GAME_OVER;
    s.game.score = 0;
    in = none();
    session_update(&s, &in, STARPOST_DT);
    CHECK(s.screen == SCREEN_TITLE);
    CHECK(s.scores.count[DIFF_ADVANCED] == 0);

    session_init(&s, 9);
    in = none();
    in.accept = true;
    tap(&s, in);
    s.game.state = STATE_GAME_OVER;
    s.game.score = 1200;
    in = none();
    session_update(&s, &in, STARPOST_DT);
    CHECK(s.screen == SCREEN_INITIALS);
    CHECK(s.pending_score == 1200);
    CHECK(s.initials[0] == 'A');
    CHECK(s.initial_slot == 0);
    CHECK(s.initials_wait == STARPOST_INITIALS_DELAY);
    CHECK(STARPOST_INITIALS_DELAY == 1.0f);
    in = none();
    in.up = true;
    session_update(&s, &in, 0.40f);
    CHECK(s.initials[0] == 'A');
    CHECK(s.initial_slot == 0);
    CHECK(s.screen == SCREEN_INITIALS);
    in = none();
    session_update(&s, &in, STARPOST_INITIALS_DELAY);
    CHECK(s.initials_wait == 0.0f);
    CHECK(s.initial_slot == 0);
    in.up = true;
    tap(&s, in);
    CHECK(s.initials[0] == 'B');
    in = none();
    in.down = true;
    tap(&s, in);
    CHECK(s.initials[0] == 'A');
    in = none();
    in.down = true;
    tap(&s, in);
    CHECK(s.initials[0] == 'Z');
    in = none();
    in.play.right = true;
    tap(&s, in);
    CHECK(s.initial_slot == 1);
    in = none();
    in.up = true;
    tap(&s, in);
    CHECK(s.initials[1] == 'B');
    CHECK(s.initials[0] == 'Z');
    in = none();
    in.accept = true;
    tap(&s, in);
    CHECK(s.screen == SCREEN_INITIALS);
    CHECK(s.initial_slot == 2);
    CHECK(s.scores.count[DIFF_ADVANCED] == 0);
    in = none();
    in.accept = true;
    tap(&s, in);
    CHECK(s.screen == SCREEN_INITIALS);
    CHECK(s.initial_slot == SCORE_INITIALS);
    in = none();
    in.up = true;
    tap(&s, in);
    CHECK(s.initials[0] == 'Z');
    CHECK(s.initials[1] == 'B');
    CHECK(s.initials[2] == 'A');
    in = none();
    in.accept = true;
    tap(&s, in);
    CHECK(s.screen == SCREEN_TITLE);
    CHECK(s.scores_dirty == 1);
    CHECK(s.scores.count[DIFF_ADVANCED] == 1);
    CHECK(s.scores.rows[DIFF_ADVANCED][0].score == 1200);
    CHECK(s.scores.rows[DIFF_ADVANCED][0].initials[0] == 'Z');
    CHECK(s.scores.rows[DIFF_ADVANCED][0].initials[1] == 'B');
    CHECK(s.scores.rows[DIFF_ADVANCED][0].initials[2] == 'A');

    session_init(&s, 11);
    in = none();
    in.accept = true;
    tap(&s, in);
    s.game.state = STATE_GAME_OVER;
    s.game.score = 800;
    in = none();
    in.accept = true;
    session_update(&s, &in, STARPOST_DT);
    CHECK(s.screen == SCREEN_INITIALS);
    session_update(&s, &in, STARPOST_INITIALS_DELAY);
    session_update(&s, &in, STARPOST_DT);
    CHECK(s.screen == SCREEN_INITIALS);
    CHECK(s.initial_slot == 0);
    CHECK(s.scores.count[DIFF_ADVANCED] == 0);
    CHECK(s.initials[0] == 'A');
}

int main(void)
{
    test_difficulty_scaling();
    test_scores_table();
    test_scores_file();
    test_session_flow();
    if (fails) {
        fprintf(stderr, "%d/%d meta checks failed\n", fails, checks);
        return 1;
    }
    printf("%d meta checks passed\n", checks);
    return 0;
}
