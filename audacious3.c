/*
 * XMP plugin for XMMS/Beep/Audacious
 * Written by Claudio Matsuoka, 2000-04-30
 * Based on J. Nick Koston's MikMod plugin for XMMS
 */

/* Audacious 3.0 port/rewrite for Fedora by Michael Schwendt
 * TODO: list of supported formats missing in 'About' dialog
 *
 * Ported for libxmp 4.0 by Claudio Matsuoka, 2013-04-13
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <sys/types.h>
#include <unistd.h>
#include <ctype.h>

#include <glib.h>
#include <gtk/gtk.h>
#include <audacious/plugin.h>
#include <audacious/misc.h>
#include <audacious/preferences.h>
#include <libaudgui/libaudgui-gtk.h>
#include <xmp.h>

#ifdef DEBUG
#else
#define _D(x...)
#endif

static GMutex *seek_mutex;
static GCond *seek_cond;
static gint jumpToTime = -1;
static gboolean stop_flag = FALSE;
static GMutex *probe_mutex;

static xmp_context ctx;

/* config values used for the Preferences UI */
struct {
	gboolean bits16, bits8;
	gboolean stereo, mono;
	gboolean freq44, freq22, freq11;
	gboolean fixloops, modrange, convert8bit, interpolation, filter;
	gfloat panamp;
} guicfg;

static void configure_init(void);

#define FREQ_SAMPLE_44 0
#define FREQ_SAMPLE_22 1
#define FREQ_SAMPLE_11 2

typedef struct {
	gint mixing_freq;
	gint force8bit;
	gint force_mono;
	gint interpolation;
	gint filter;
	gint convert8bit;
	gint fixloops;
	gint loop;
	gint modrange;
	gint pan_amplitude;
	gint time;
	struct xmp_module_info mod_info;
} XMPConfig;

XMPConfig plugin_cfg;

static const gchar* const plugin_defaults[] = {
    "mixing_freq", "0",
    "convert8bit", "0",
    "fixloops", "0",
    "modrange", "0",
    "force8bit", "0",
    "force_mono", "0",
    "interpolation", "TRUE",
    "filter", "TRUE",
    "pan_amplitude", "80",
    NULL
};

extern struct xmp_drv_info drv_smix;


static void strip_vfs(char *s) {
	int len;
	char *c;

	if (!s) {
		return;
	}
	_D("%s", s);
	if (!memcmp(s, "file://", 7)) {
		len = strlen(s);
		memmove(s, s + 7, len - 6);
	}

	for (c = s; *c; c++) {
		if (*c == '%' && isxdigit(*(c + 1)) && isxdigit(*(c + 2))) {
			char val[3];
			val[0] = *(c + 1);
			val[1] = *(c + 2);
			val[2] = 0;
			*c++ = strtoul(val, NULL, 16);
			len = strlen(c);
			memmove(c, c + 2, len - 1);
		}
	}
}


static void stop(InputPlayback *playback)
{
	_D("*** stop!");
	g_mutex_lock(seek_mutex);
	if (!stop_flag) {
		xmp_stop_module(ctx); 
		stop_flag = TRUE;
		playback->output->abort_write();
		g_cond_signal(seek_cond);
	}
	g_mutex_unlock(seek_mutex);
}


static void mseek(InputPlayback *playback, gint msec)
{
	g_mutex_lock(seek_mutex);
	if (!stop_flag) {
		jumpToTime = msec;
		playback->output->abort_write();
		g_cond_signal(seek_cond);
		g_cond_wait(seek_cond, seek_mutex);
	}
	g_mutex_unlock(seek_mutex);
}


static void seek_ctx(gint time)
{
	xmp_seek_time(ctx, time);
}


static void mod_pause(InputPlayback *playback, gboolean p)
{
	g_mutex_lock(seek_mutex);
	if (!stop_flag) {
		playback->output->pause(p);
	}
	g_mutex_unlock(seek_mutex);
}


static gboolean init(void)
{
	_D("Plugin init");
	ctx = xmp_create_context();

	probe_mutex = g_mutex_new();
	jumpToTime = -1;
	seek_mutex = g_mutex_new();
	seek_cond = g_cond_new();

	aud_config_set_defaults("XMP",plugin_defaults);

#define CFGREADINT(x) plugin_cfg.x = aud_get_int ("XMP", #x)

	CFGREADINT(mixing_freq);
	CFGREADINT(force8bit);
	CFGREADINT(convert8bit);
	CFGREADINT(modrange);
	CFGREADINT(fixloops);
	CFGREADINT(force_mono);
	CFGREADINT(interpolation);
	CFGREADINT(filter);
	CFGREADINT(pan_amplitude);

	configure_init();

	//xmp_init(ctx, 0, NULL);

	return TRUE;
}


static void cleanup()
{
	g_cond_free(seek_cond);
	g_mutex_free(seek_mutex);
    g_mutex_free(probe_mutex);
	xmp_free_context(ctx);
}


static int is_our_file_from_vfs(const char* _filename, VFSFile *vfsfile)
{
	gchar *filename = g_strdup(_filename);
	gboolean ret;

	_D("filename = %s", filename);
	strip_vfs(filename);		/* Sorry, no VFS support */

	ret = (xmp_test_module(filename, NULL) == 0);

	g_free(filename);
	return ret;
}


Tuple *probe_for_tuple(const gchar *_filename, VFSFile *fd)
{
	gchar *filename = g_strdup(_filename);
	xmp_context ctx;
	Tuple *tuple;
	struct xmp_module_info mi;
	struct xmp_frame_info fi;

	g_mutex_lock(probe_mutex);
	_D("filename = %s", filename);
	strip_vfs(filename);		/* Sorry, no VFS support */

	ctx = xmp_create_context();
	xmp_enable_sample_load(ctx, 0);
	if (xmp_load_module(ctx, filename) < 0) {
        	g_free(filename);
		xmp_free_context(ctx);
        	g_mutex_unlock(probe_mutex);
		return NULL;
	}

	xmp_get_module_info(ctx, &mi);
	xmp_get_frame_info(ctx, &fi);

	tuple = tuple_new_from_filename(filename);
	g_free(filename);
	tuple_set_str(tuple, FIELD_TITLE, NULL, mi.mod->name);
	tuple_set_str(tuple, FIELD_CODEC, NULL, mi.mod->type);
	tuple_set_int(tuple, FIELD_LENGTH, NULL, fi.total_time);

	xmp_release_module(ctx);
	xmp_free_context(ctx);

	g_mutex_unlock(probe_mutex);

	return tuple;
}


static gboolean play(InputPlayback *ipb, const gchar *_filename, VFSFile *file, gint start_time, gint stop_time, gboolean pause)
{
	int channelcnt;
	FILE *f;
	struct xmp_frame_info fi;
	int lret, fmt;
	gchar *filename = g_strdup(_filename);
	Tuple *tuple;
	int freq, resol, flags, dsp;
	
	_D("play: %s\n", filename);
	if (file == NULL) {
		return FALSE;
	}
	//opt = xmp_get_options(ctx);

	strip_vfs(filename);  /* Sorry, no VFS support */

	_D("play_file: %s", filename);

	jumpToTime = (start_time > 0) ? start_time : -1;
	stop_flag = FALSE;

	if ((f = fopen(filename, "rb")) == 0) {
		goto PLAY_ERROR_1;
	}
	fclose(f);

	resol = 8;
	flags = 0;
	channelcnt = 1;

	switch (plugin_cfg.mixing_freq) {
	case 1:
		freq = 22050;		/* 1:2 mixing freq */
		break;
	case 2:
		freq = 11025;		/* 1:4 mixing freq */
		break;
	default:
		freq = 44100;		/* standard mixing freq */
		break;
	}

	if (plugin_cfg.force8bit == 0)
		resol = 16;

	if (plugin_cfg.force_mono == 0) {
		channelcnt = 2;
		flags &= ~XMP_FORMAT_MONO;
	} else {
		flags |= XMP_FORMAT_MONO;
	}

	if (plugin_cfg.interpolation == 1)
		xmp_set_player(ctx, XMP_PLAYER_INTERP, XMP_INTERP_SPLINE);
	else
		xmp_set_player(ctx, XMP_PLAYER_INTERP, XMP_INTERP_NEAREST);

	dsp = xmp_get_player(ctx, XMP_PLAYER_DSP);
	if (plugin_cfg.filter == 1)
		dsp |= XMP_DSP_LOWPASS;
	else
		dsp &= ~XMP_DSP_LOWPASS;

	xmp_set_player(ctx, XMP_PLAYER_MIX, plugin_cfg.pan_amplitude);

	fmt = resol == 16 ? FMT_S16_NE : FMT_U8;
	
	if (!ipb->output->open_audio(fmt, freq, channelcnt)) {
		goto PLAY_ERROR_1;
	}

	//xmp_open_audio(ctx);

	_D("*** loading: %s", filename);

	lret =  xmp_load_module(ctx, filename);

	if (lret < 0) {
		//xmp_close_audio(ctx);
		goto PLAY_ERROR_1;
	}

	//plugin_cfg.time = lret;
	xmp_get_module_info(ctx, &plugin_cfg.mod_info);

	tuple = tuple_new_from_filename(filename);
	g_free(filename);
	tuple_set_str(tuple, FIELD_TITLE, NULL, plugin_cfg.mod_info.mod->name);
	tuple_set_str(tuple, FIELD_CODEC, NULL, plugin_cfg.mod_info.mod->type);
	tuple_set_int(tuple, FIELD_LENGTH, NULL, lret);
	ipb->set_tuple(ipb, tuple);

	ipb->set_params(ipb, plugin_cfg.mod_info.mod->chn * 1000, freq, channelcnt);
	ipb->set_pb_ready(ipb);

	stop_flag = FALSE;
	xmp_start_player(ctx, freq, flags);

	while (!stop_flag) {
		if (stop_time >= 0 && ipb->output->written_time() >= stop_time) {
			goto DRAIN;
        	}

		g_mutex_lock(seek_mutex);
		if (jumpToTime != -1) {
			seek_ctx(jumpToTime);
			ipb->output->flush(jumpToTime);
			jumpToTime = -1;
			g_cond_signal(seek_cond);
		}
		g_mutex_unlock(seek_mutex);

		xmp_get_frame_info(ctx, &fi);

        	if (!stop_flag && jumpToTime < 0) {
			ipb->output->write_audio(fi.buffer, fi.buffer_size);
        	}

		if ((xmp_play_frame(ctx) != 0) && jumpToTime < 0) {
			stop_flag = TRUE;
 DRAIN:
			break;
		}
	}

	g_mutex_lock(seek_mutex);
	stop_flag = TRUE;
	g_cond_signal(seek_cond);  /* wake up any waiting request */
	g_mutex_unlock(seek_mutex);

	xmp_end_player(ctx);
	xmp_release_module(ctx);
	//xmp_close_audio(ctx);
	return TRUE;

 PLAY_ERROR_1:
	g_free(filename);
	return FALSE;
}


static void configure_apply()
{
	/* transfer Preferences UI config values back into XMPConfig */
	if (guicfg.freq11) {
		plugin_cfg.mixing_freq = FREQ_SAMPLE_11;
	} else if (guicfg.freq22) {
		plugin_cfg.mixing_freq = FREQ_SAMPLE_22;
	} else {  /* if (guicfg.freq44) { */
		plugin_cfg.mixing_freq = FREQ_SAMPLE_44;
	}

	plugin_cfg.convert8bit = guicfg.bits8;
	plugin_cfg.force_mono = guicfg.mono;
	plugin_cfg.fixloops = guicfg.fixloops;
	plugin_cfg.modrange = guicfg.modrange;
	plugin_cfg.interpolation = guicfg.interpolation;
	plugin_cfg.filter = guicfg.filter;
	plugin_cfg.pan_amplitude = (gint)guicfg.panamp;

	//opt = xmp_get_options(ctx);
	xmp_set_player(ctx, XMP_PLAYER_MIX, plugin_cfg.pan_amplitude);

#define CFGWRITEINT(x) aud_set_int ("XMP", #x, plugin_cfg.x)

	CFGWRITEINT(mixing_freq);
	CFGWRITEINT(force8bit);
	CFGWRITEINT(convert8bit);
	CFGWRITEINT(modrange);
	CFGWRITEINT(fixloops);
	CFGWRITEINT(force_mono);
	CFGWRITEINT(interpolation);
	CFGWRITEINT(filter);
	CFGWRITEINT(pan_amplitude);
}

static void configure_init(void)
{
	/* transfer XMPConfig into Preferences UI config */
	/* keeping compatibility with older releases */
	guicfg.freq11 = (plugin_cfg.mixing_freq == FREQ_SAMPLE_11);
	guicfg.freq22 = (plugin_cfg.mixing_freq == FREQ_SAMPLE_22);
	guicfg.freq44 = (plugin_cfg.mixing_freq == FREQ_SAMPLE_44);
	guicfg.mono = plugin_cfg.force_mono;
	guicfg.stereo = !plugin_cfg.force_mono;
	guicfg.bits8 = plugin_cfg.convert8bit;
	guicfg.bits16 = !plugin_cfg.convert8bit;
	guicfg.convert8bit = plugin_cfg.convert8bit;
	guicfg.fixloops = plugin_cfg.fixloops;
	guicfg.modrange = plugin_cfg.modrange;
	guicfg.interpolation = plugin_cfg.interpolation;
	guicfg.filter = plugin_cfg.filter;
	guicfg.panamp = plugin_cfg.pan_amplitude;
}

void plugin_aud_about()
{
	static GtkWidget *about_window = NULL;

	audgui_simple_message(&about_window, GTK_MESSAGE_INFO,
                          g_strdup_printf(
                "Extended Module Player %s", VERSION),
                "Written by Claudio Matsuoka and Hipolito Carraro Jr.\n"
                "\n"
		"Audacious 3 plugin by Michael Schwendt\n"
                "\n"
                "Portions Copyright (C) 1998,2000 Olivier Lapicque,\n"
                "(C) 1998 Tammo Hinrichs, (C) 1998 Sylvain Chipaux,\n"
                "(C) 1997 Bert Jahn, (C) 1999 Tatsuyuki Satoh, (C)\n"
                "1995-1999 Arnaud Carre, (C) 2001-2006 Russell Marks,\n"
		"(C) 2005-2006 Michael Kohn\n"
                "\n"
		/* TODO: list */
		/* "Supported module formats:" */
	);
}


static PreferencesWidget prefs_precision[] = {
	{ WIDGET_RADIO_BTN, "16 bit", &guicfg.bits16,
		NULL, NULL, FALSE, .cfg_type = VALUE_BOOLEAN },
	{ WIDGET_RADIO_BTN, "8 bit", &guicfg.bits8,
		NULL, NULL, FALSE, .cfg_type = VALUE_BOOLEAN },
};

static PreferencesWidget prefs_channels[] = {
	{ WIDGET_RADIO_BTN, "Stereo", &guicfg.stereo,
		NULL, NULL, FALSE, .cfg_type = VALUE_BOOLEAN },
	{ WIDGET_RADIO_BTN, "Mono", &guicfg.mono,
		NULL, NULL, FALSE, .cfg_type = VALUE_BOOLEAN },
};

static PreferencesWidget prefs_frequency[] = {
	{ WIDGET_RADIO_BTN, "44 kHz", &guicfg.freq44,
		NULL, NULL, FALSE, .cfg_type = VALUE_BOOLEAN },
	{ WIDGET_RADIO_BTN, "22 kHz", &guicfg.freq22,
		NULL, NULL, FALSE, .cfg_type = VALUE_BOOLEAN },
	{ WIDGET_RADIO_BTN, "11 kHz", &guicfg.freq11,
		NULL, NULL, FALSE, .cfg_type = VALUE_BOOLEAN },
};

static PreferencesWidget prefs_opts[] = {
	{ WIDGET_CHK_BTN, "Convert 16 bit samples to 8 bit",
		&guicfg.convert8bit, NULL, NULL, FALSE,
		.cfg_type = VALUE_BOOLEAN },
	{ WIDGET_CHK_BTN, "Fix sample loops", &guicfg.fixloops,
		NULL, NULL, FALSE, .cfg_type = VALUE_BOOLEAN },
	{ WIDGET_CHK_BTN, "Force 3 octave range in standard MOD files",
		&guicfg.modrange, NULL, NULL, FALSE,
		.cfg_type = VALUE_BOOLEAN },
	{ WIDGET_CHK_BTN, "Enable 32-bit linear interpolation",
		&guicfg.interpolation, NULL, NULL, FALSE,
		.cfg_type = VALUE_BOOLEAN },
	{ WIDGET_CHK_BTN, "Enable IT filters", &guicfg.filter,
		NULL, NULL, FALSE, .cfg_type = VALUE_BOOLEAN},
	{ WIDGET_LABEL, "Pan amplitude (%)", NULL, NULL, NULL, FALSE },
	{ WIDGET_SPIN_BTN, "", &guicfg.panamp, NULL, NULL, FALSE,
      .cfg_type = VALUE_FLOAT, NULL, NULL,
      { .spin_btn = { 0.0, 100.0, 1.0, "" } }
    },
};

static PreferencesWidget prefs_opts_tab[] = {
	{ WIDGET_BOX, NULL, NULL, NULL, NULL, FALSE, VALUE_NULL, NULL, NULL,
		{.box = { prefs_opts, G_N_ELEMENTS(prefs_opts), FALSE, FALSE}}},
};

static PreferencesWidget prefs_qual_row1[] = {
	{ WIDGET_BOX, "Resolution", NULL, NULL, NULL, FALSE, VALUE_NULL, NULL, NULL,
		{ .box = { prefs_precision, G_N_ELEMENTS(prefs_precision),
			FALSE, TRUE }
		}
	},
	{ WIDGET_BOX, "Channels", NULL, NULL, NULL, FALSE, VALUE_NULL, NULL, NULL,
		{ .box = { prefs_channels, G_N_ELEMENTS(prefs_channels),
			FALSE, TRUE }
		}
	},
};

static PreferencesWidget prefs_qual_row2[] = {
	{ WIDGET_BOX, "Sampling rate", NULL, NULL, NULL, FALSE, VALUE_NULL, NULL, NULL,
		{ .box = { prefs_frequency, G_N_ELEMENTS(prefs_frequency),
			FALSE, TRUE }
		}
	},
};

static PreferencesWidget prefs_qual_box1[] = {
	{ WIDGET_BOX, NULL, NULL, NULL, NULL, FALSE, VALUE_NULL, NULL, NULL,
		{ .box = { prefs_qual_row1, G_N_ELEMENTS(prefs_qual_row1),
			TRUE, TRUE }
		}
	},
	{ WIDGET_BOX, NULL, NULL, NULL, NULL, FALSE, VALUE_NULL, NULL, NULL,
		{ .box = { prefs_qual_row2, G_N_ELEMENTS(prefs_qual_row2),
			FALSE, TRUE }
		}
	},
};

static PreferencesWidget prefs_qual_tab[] = {
	{ WIDGET_BOX, NULL, NULL, NULL, NULL, FALSE, VALUE_NULL, NULL, NULL,
		{ .box = { prefs_qual_box1, G_N_ELEMENTS(prefs_qual_box1),
			FALSE, TRUE }
		}
	},
};

static NotebookTab prefs_tabs[] = {
	{ "Quality", prefs_qual_tab, G_N_ELEMENTS(prefs_qual_tab) },
	{ "Options", prefs_opts_tab, G_N_ELEMENTS(prefs_opts_tab) },
};

static PreferencesWidget prefs[] = {
	{WIDGET_NOTEBOOK, NULL, NULL, NULL, NULL, FALSE, VALUE_NULL, NULL, NULL,
		{ .notebook = { prefs_tabs, G_N_ELEMENTS(prefs_tabs) } },
	},
};

PluginPreferences plugin_aud_preferences = {
	.prefs = prefs,
	.n_prefs = G_N_ELEMENTS(prefs),
	.init = configure_init,
	.apply = configure_apply,
};

/* Filtering files by suffix isn't good for modules. */
const gchar* const fmts[] = {
	"xm", "mod", "m15", "it", "s2m", "s3m", "stm", "stx", "med", "dmf",
	"mtm", "ice", "imf", "ptm", "mdl", "ult", "liq", "psm", "amf",
        "rtm", "pt3", "tcb", "dt", "gtk", "dtt", "mgt", "digi", "dbm",
	"emod", "okt", "sfx", "far", "umx", "stim", "mtp", "ims", "669",
	"fnk", "funk", "amd", "rad", "hsc", "alm", "kris", "ksm", "unic",
	"zen", "crb", "tdd", "gmc", "gdm", "mdz", "xmz", "s3z", "j2b", NULL
};

AUD_INPUT_PLUGIN (
	.name		= "XMP Plugin " VERSION,
	.init		= init,
	.about		= plugin_aud_about,
	.settings	= &plugin_aud_preferences,
	.play		= play,
	.stop		= stop,
	.pause		= mod_pause,
	.probe_for_tuple = probe_for_tuple,
	.is_our_file_from_vfs = is_our_file_from_vfs,
	.cleanup	= cleanup,
	.mseek		= mseek,
	.extensions	= fmts,
)

