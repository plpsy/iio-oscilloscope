/**
 * Copyright (C) 2019 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <math.h>

#include "../datatypes.h"
#include "../osc.h"
#include "../osc_plugin.h"
#include "../iio_widget.h"
#include "../config.h"
#include "./block_diagram.h"

#define THIS_DRIVER			"CN0540_AIC"
#define ADC_DEVICE0			"cf_axi_adc"
#define ADC_DEVICE1			"cf_axi_adc_1"

#define DAC_DEVICE			"ltc2606_0"
#define DAC_DEVICE_PRIFIX			"ltc2606_"
#define VOLTAGE_MONITOR_1		"xadc"
#define VOLTAGE_MONITOR_2		"ltc2308"
#define ADC_DEVICE_CH			"voltage0"
#define DAC_DEVICE_CH			"voltage0"

#define G				0.3
#define FDA_VOCM_MV			2500
#define FDA_GAIN			2.667
#define DAC_BUF_GAIN			1.22
#define CALIB_MAX_ITER		  	20
#define XADC_VREF			3.3
#define XADC_RES			12
#define NUM_GPIOS			8
#define NUM_ANALOG_PINS		 	6

#define TOTOL_CHAN	16
#define AD7768_NUM		2
#define AD7768_CHAN 	8

struct iio_device *iio_adc[AD7768_NUM];
struct iio_device *iio_dac[TOTOL_CHAN];
static struct iio_context *ctx;
static struct iio_channel *adc_ch[TOTOL_CHAN];
static struct iio_channel *dac_ch[TOTOL_CHAN];

static GtkWidget *cn0540_panel;

static GtkButton *calib_btn[TOTOL_CHAN];
static GtkButton *write_btn[TOTOL_CHAN];
static GtkButton *read_btn[TOTOL_CHAN];
static GtkButton *readvsensor_btn[TOTOL_CHAN];

static GtkTextView *vshift_log[TOTOL_CHAN];
static GtkTextView *vsensor_log[TOTOL_CHAN];
static GtkTextView *calib_status[TOTOL_CHAN];


static GtkTextBuffer *calib_buffer[TOTOL_CHAN];
static GtkTextBuffer *vsensor_buf[TOTOL_CHAN];
static GtkTextBuffer *vshift_buf[TOTOL_CHAN];


static gboolean plugin_detached;
static gint this_page;

struct osc_plugin plugin;

void delay_ms(int ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000 * 1000 };
	nanosleep(&ts, NULL);

}

static double get_voltage(struct iio_channel *ch)
{
	double scale;
	long long raw;

	iio_channel_attr_read_longlong(ch,"raw",&raw);
	iio_channel_attr_read_double(ch,"scale",&scale);
	return raw * scale;
}

static void set_voltage(struct iio_channel *ch, double voltage_mv)
{
	double scale;

	iio_channel_attr_read_double(ch,"scale",&scale);
	iio_channel_attr_write_longlong(ch,"raw",
					(long long)(voltage_mv / scale));
}

static double get_vshift_mv(int chan)
{
	return get_voltage(dac_ch[chan]) * DAC_BUF_GAIN;
}

static void read_vshift(GtkButton *btn, int chan)
{
	char buff_string[9];
	double vshift_mv;
	if(chan < 0 && chan > 15)
	{
		exit(1);
	}
	vshift_mv = get_vshift_mv(chan);

	snprintf(buff_string, sizeof(buff_string), "%f", vshift_mv);
	gtk_text_buffer_set_text(vshift_buf[chan], buff_string, -1);
}

static void write_vshift(GtkButton *btn, int chan)
{
	static GtkTextIter start, end;
	gchar *vshift_string;
	double vshift_mv;

	gtk_text_buffer_get_start_iter(vshift_buf[chan], &start);
	gtk_text_buffer_get_end_iter(vshift_buf[chan], &end);
	vshift_string = gtk_text_buffer_get_text(vshift_buf[chan],&start,&end, -1);
	vshift_mv = atof(vshift_string);
	set_voltage(dac_ch[chan], (double)(vshift_mv / DAC_BUF_GAIN));

	g_free(vshift_string);
	fflush(stdout);
}

static void read_vsensor(GtkButton *btn, int chan)
{

	char buff_string[9];
	double vsensor_mv, vshift_mv,vadc_mv;

	vadc_mv = get_voltage(adc_ch[chan]);
	vshift_mv = get_vshift_mv(chan);
	double v1_st = FDA_VOCM_MV - vadc_mv/FDA_GAIN;
	vsensor_mv = (((G + 1) *vshift_mv ) - v1_st )/G;
	vsensor_mv -= get_voltage(adc_ch[chan]);

	snprintf(buff_string, sizeof(buff_string), "%f", vsensor_mv);
	gtk_text_buffer_set_text(vsensor_buf[chan], buff_string, -1);
}

static void calib(GtkButton *btn, int chan)
{
	double adc_voltage_mv, dac_voltage_mv;
	char buff_string[9];
	int i;

	gtk_text_buffer_set_text(calib_buffer[chan], "Calibrating...", -1);

	for(i = 0; i < CALIB_MAX_ITER; i ++)
	{
		adc_voltage_mv = get_voltage(adc_ch[chan]);
		dac_voltage_mv = get_voltage(dac_ch[chan]) - adc_voltage_mv;
		set_voltage(dac_ch[chan], dac_voltage_mv);
		delay_ms(10);
	}
	snprintf(buff_string, sizeof(buff_string), "%f", adc_voltage_mv);
	gtk_text_buffer_set_text(calib_buffer[chan], buff_string, -1);
	gtk_button_clicked(read_btn[chan]);
	gtk_button_clicked(readvsensor_btn[chan]);
}


static void cn0540_get_channels()
{
	int devidx = 0;
	int chanidx = 0;
	int idx = 0;
	char label[64];

	for(devidx = 0; devidx < AD7768_NUM; devidx++)
	{
		for(chanidx = 0; chanidx < AD7768_CHAN; chanidx++)
		{
			idx = (devidx * AD7768_CHAN) + chanidx;
			memset(label, 0, sizeof(label));
			sprintf(label, "voltage%d", chanidx);
			adc_ch[idx] = iio_device_find_channel(iio_adc[devidx], label, FALSE);
			if(adc_ch[idx] == NULL)
			{
				exit(1);
			}
		}
	}

	for(devidx = 0; devidx < TOTOL_CHAN; devidx++)
	{
		dac_ch[devidx] = iio_device_find_channel(iio_dac[devidx], DAC_DEVICE_CH, TRUE);
		if(dac_ch[devidx] == NULL)
		{
			exit(2);
		}
	}
}

static void cn0540_plugin_interface_init(GtkBuilder *builder)
{
	int i = 0;
	char name[64];

	cn0540_panel = GTK_WIDGET(gtk_builder_get_object(builder,
				"cn0540_panel"));

	for(i = 0; i < TOTOL_CHAN; i++)
	{
		memset(name, 0, sizeof(name));
		sprintf(name, "calib_btn%d", i);
		calib_btn[i] = GTK_BUTTON(gtk_builder_get_object(builder, name));
		if(calib_btn[i] == NULL)
		{
			exit(1);
		}
		memset(name, 0, sizeof(name));
		sprintf(name, "read_btn%d", i);
		read_btn[i] = GTK_BUTTON(gtk_builder_get_object(builder, name));
		if(read_btn[i] == NULL)
		{
			exit(1);
		}

		memset(name, 0, sizeof(name));
		sprintf(name, "write_btn%d", i);
		write_btn[i] = GTK_BUTTON(gtk_builder_get_object(builder, name));
		if(write_btn[i] == NULL)
		{
			exit(1);
		}

		memset(name, 0, sizeof(name));
		sprintf(name, "readvsensor_btn%d", i);
		readvsensor_btn[i] = GTK_BUTTON(gtk_builder_get_object(builder, name));
		if(readvsensor_btn[i] == NULL)
		{
			exit(1);
		}

		memset(name, 0, sizeof(name));
		sprintf(name, "calib_status%d", i);
		calib_status[i] = GTK_TEXT_VIEW(gtk_builder_get_object(builder, name));
		if(calib_status[i] == NULL)
		{
			exit(1);
		}

		memset(name, 0, sizeof(name));
		sprintf(name, "calib_buffer%d", i);
		calib_buffer[i] = GTK_TEXT_BUFFER(gtk_builder_get_object(builder, name));
		if(calib_buffer[i] == NULL)
		{
			exit(1);
		}

		memset(name, 0, sizeof(name));
		sprintf(name, "vshift_log%d", i);
		vshift_log[i] = GTK_TEXT_VIEW(gtk_builder_get_object(builder, name));
		if(vshift_log[i] == NULL)
		{
			exit(1);
		}

		memset(name, 0, sizeof(name));
		sprintf(name, "vshift_buf%d", i);
		vshift_buf[i] = GTK_TEXT_BUFFER(gtk_builder_get_object(builder, name));
		if(vshift_buf[i] == NULL)
		{
			exit(1);
		}

		memset(name, 0, sizeof(name));
		sprintf(name, "vsensor_log%d", i);
		vsensor_log[i] = GTK_TEXT_VIEW(gtk_builder_get_object(builder, name));
		if(vsensor_log[i] == NULL)
		{
			exit(1);
		}

		memset(name, 0, sizeof(name));
		sprintf(name, "vsensor_buf%d", i);
		vsensor_buf[i] = GTK_TEXT_BUFFER(gtk_builder_get_object(builder, name));
		if(vsensor_buf[i] == NULL)
		{
			exit(1);
		}

		g_signal_connect(G_OBJECT(calib_btn[i]),"clicked",
			G_CALLBACK(calib), i);
		g_signal_connect(G_OBJECT(read_btn[i]),"clicked",
			G_CALLBACK(read_vshift), i);
		g_signal_connect(G_OBJECT(write_btn[i]),"clicked",
			G_CALLBACK(write_vshift), i);
		g_signal_connect(G_OBJECT(readvsensor_btn[i]),"clicked",
			G_CALLBACK(read_vsensor), i);
	}
}

static void cn0540_init()
{
	int idx;

//	gtk_button_clicked(calib_btn[0]);

}

static GtkWidget *cn0540_plugin_init(struct osc_plugin *plugin,
				     GtkWidget *notebook, const char *ini_fn)
{
	GtkBuilder *builder;

	builder = gtk_builder_new();

	ctx = osc_create_context();
	if (!ctx)
		return NULL;

	if (osc_load_glade_file(builder, "cn0540_aic") < 0) {
		osc_destroy_context(ctx);
		return NULL;
	}

	block_diagram_init(builder, 1, "CN0540.jpg");

	cn0540_get_channels();
	cn0540_plugin_interface_init(builder);
	cn0540_init();

	return cn0540_panel;
}

static bool cn0540_identify(const struct osc_plugin *plugin)
{
	int i = 0;
	char dac_dev_name[64];
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();

	/* Get the iio devices */
	iio_adc[0] = iio_context_find_device(osc_ctx, ADC_DEVICE0);
	iio_adc[1] = iio_context_find_device(osc_ctx, ADC_DEVICE1);
	if (iio_adc[0] == NULL || iio_adc[1] == NULL)
	{
		return FALSE;
	}

	for(i = 0; i < TOTOL_CHAN; i++)
	{
		memset(dac_dev_name, 0, sizeof(dac_dev_name));
		sprintf(dac_dev_name, "ltc2606_%d", i); 
		iio_dac[i] = iio_context_find_device(osc_ctx, dac_dev_name);
		if(iio_dac[i] == NULL) 
		{
			return FALSE;			
		}
	}

	return TRUE;
}

static void context_destroy(struct osc_plugin *plugin, const char *ini_fn)
{
	osc_destroy_context(ctx);
}

static void cn0540_get_preferred_size(const struct osc_plugin *plugin,
					  int *width, int *height)
{
	if (width)
		*width = 640;
	if (height)
		*height = 480;
}

static void update_active_page(struct osc_plugin *plugin, gint active_page,
				   gboolean is_detached)
{
	this_page = active_page;
	plugin_detached = is_detached;
}

struct osc_plugin plugin = {
	.name = THIS_DRIVER,
	.identify = cn0540_identify,
	.init = cn0540_plugin_init,
	.update_active_page = update_active_page,
	.get_preferred_size = cn0540_get_preferred_size,
	.destroy = context_destroy,
};
