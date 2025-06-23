/*
 *  ESCP-Printer App
 *  Copyright (C) 2025  Ada Hartley, and Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <endian.h>
#include <pappl/pappl.h>

static pappl_pr_driver_t escp_drivers[] = {
	{"epson_9pin_series", "Epson 9-Pin Series", "CMD:ESCP9;", NULL}
};

typedef struct escp_s
{
	float *buffer;
	unsigned char *line;
	int line_width;
	int line_offset;
	int page_length;
	int page_offset;
	int buffer_lines;
	int print_lines;
	int fields;
	char dot_density;
	float exponent;
	FILE *file;
} escp_t;

float *escp_getline(escp_t *escp, int line)
{
	return escp->buffer + (line % escp->buffer_lines) * escp->line_width;
}

bool escp_printfile(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device)
{
	int fd = open(papplJobGetFilename(job), O_RDONLY);
	ssize_t bytes;
	char buffer[65536];

	papplJobSetImpressions(job, 1);

	while ((bytes = read(fd, buffer, sizeof(buffer))) > 0)
	{
		if (papplDeviceWrite(device, buffer, (size_t)bytes) < 0)
		{
			papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Unable to send %d bytes to printer.", (int)bytes);
			close(fd);
			return false;
		}
	}

	close(fd);

	papplJobSetImpressionsCompleted(job, 1);

	return true;
}

bool escp_rstartjob(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device)
{
	escp_t *escp = malloc(sizeof(escp_t));
	escp->buffer = NULL;
	escp->line = NULL;

	papplJobSetData(job, escp);

	if (papplDevicePuts(device, "\033@\r") < 0) return false;

	return true;
}

bool escp_rstartpage(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, unsigned page)
{
	escp_t *escp = (escp_t *)papplJobGetData(job);

	switch (options->printer_resolution[0])
	{
	case 60:
		escp->fields = 1;
		escp->exponent = 1.0 / 1.0;
		escp->dot_density = '0';
		break;
	case 120:
		escp->fields = 2;
		escp->exponent = 1.0 / 2.0;
		escp->dot_density = '1';
		break;
	case 240:
		escp->fields = 3;
		escp->exponent = 1.0 / 4.0;
		escp->dot_density = '3';
		break;
	}

	escp->print_lines = escp->fields * 8;
	escp->buffer_lines = escp->print_lines + 2;

	escp->line_width = (options->printer_resolution[0] * (options->media.size_width - options->media.left_margin - options->media.right_margin)) / 2540;
	escp->line_offset = (options->printer_resolution[0] * options->media.left_margin) / 2540;

	escp->page_length = (options->printer_resolution[1] * (options->media.size_length - options->media.top_margin - options->media.bottom_margin)) / 2540;
	escp->page_offset = (options->printer_resolution[1] * options->media.top_margin) / 2540;

	escp->buffer = realloc(escp->buffer, escp->line_width * escp->buffer_lines * sizeof(float));
	memset(escp->buffer, 0, escp->line_width * escp->buffer_lines * sizeof(float));

	escp->line = realloc(escp->line, escp->line_width);

//	escp->file = fopen("test.data", "wb");

	return true;
}

bool escp_rwriteline(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, unsigned y, const unsigned char *line)
{
	escp_t *escp = (escp_t *)papplJobGetData(job);

	y -= escp->page_offset;

	if (y >= 0 && y < escp->page_length)
	{
		for (int i = 0; i < escp->line_width; i++)
		{
			float *pixel = escp_getline(escp, y) + i;
			*pixel += powf((float)line[i + escp->line_offset] / 255.0, escp->exponent);
			float new_pixel = *pixel >= 0.5 ? 1.0 : 0.0;
			float error = new_pixel - *pixel;
			*pixel = new_pixel;

			const int var[] = {
				         7, 5,
				3, 5, 7, 5, 3,
				1, 3, 5, 3, 1
			};

			for (int j = 3; j < sizeof(var) / sizeof(var[0]) + 3; j++) {
				int x1 = i + j % 5;
				int y1 = y + j / 5;
				if (x1 >= 0 && x1 < escp->line_width)
					escp_getline(escp, y1)[x1] -= (error * var[j]) / 48.0;
			}
		}
//		fwrite(escp_getline(escp, y), sizeof(float), escp->line_width, escp->file);

		if (y % escp->print_lines == escp->print_lines - 1)
		{
			for (int field = 0; field < escp->fields; field++)
			{
				for (int x = 0; x < escp->line_width; x++)
				{
					unsigned char byte = 0;
					for (int i = 0; i < escp->print_lines; i += escp->fields)
					{
						byte <<= 1;
						byte |= escp_getline(escp, y - (escp->print_lines - 1) + i + field)[x] < 0.5;
					}
					escp->line[x] = byte;
				}

				uint16_t dot_columns = htole16(escp->line_width);
				uint8_t shift = (field == escp->fields - 1) ? (25 - escp->fields) : 1;

				if (papplDevicePuts(device, "\033*") < 0) return false;
				if (papplDeviceWrite(device, &escp->dot_density, sizeof(escp->dot_density)) < 0) return false;
				if (papplDeviceWrite(device, &dot_columns, sizeof(dot_columns)) < 0) return false;
				if (papplDeviceWrite(device, escp->line, escp->line_width) < 0) return false;
				if (papplDevicePuts(device, "\r\033J") < 0) return false;
				if (papplDeviceWrite(device, &shift, sizeof(shift)) < 0) return false;
			}
			papplDeviceFlush(device);
		}

		memset(escp->buffer + ((y + 3) % escp->buffer_lines) * escp->line_width, 0, escp->line_width * sizeof(float));
	}

	return true;
}

bool escp_rendpage(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, unsigned page)
{
	escp_t *escp = (escp_t *)papplJobGetData(job);

//	fclose(escp->file);
	if (papplDevicePuts(device, "\f") < 0) return false;

	return true;
}

bool escp_rendjob(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device)
{
	escp_t *escp = (escp_t *)papplJobGetData(job);

	free(escp->buffer);
	free(escp->line);
	free(escp);

	return true;
}

bool escp_status(pappl_printer_t *printer)
{
	return true;
}

void escp_identify(pappl_printer_t *printer, pappl_identify_actions_t actions, const char *message)
{
	if (actions && PAPPL_IDENTIFY_ACTIONS_SOUND)
	{
		pappl_device_t *device = papplPrinterOpenDevice(printer);
		if (device == NULL) return;
		papplDevicePuts(device, "\033@\a");
		papplDeviceFlush(device);
	}
}

bool escp_callback(pappl_system_t *system, const char *driver_name, const char *device_uri, const char *device_id, pappl_pr_driver_data_t *driver_data, ipp_t **driver_attrs, void *data)
{
	driver_data->printfile_cb = escp_printfile;
	driver_data->rstartjob_cb = escp_rstartjob;
	driver_data->rstartpage_cb = escp_rstartpage;
	driver_data->rwriteline_cb = escp_rwriteline;
	driver_data->rendpage_cb = escp_rendpage;
	driver_data->rendjob_cb = escp_rendjob;
	driver_data->status_cb = escp_status;
	driver_data->identify_cb = escp_identify;
	driver_data->has_supplies = true;

	driver_data->format = "application/x-epson-escp9";

	driver_data->identify_supported = PAPPL_IDENTIFY_ACTIONS_SOUND;
	driver_data->identify_default = PAPPL_IDENTIFY_ACTIONS_SOUND;

	driver_data->orient_default = IPP_ORIENT_NONE;
	driver_data->quality_default = IPP_QUALITY_NORMAL;

	if (!strcmp(driver_name, "epson_9pin_series"))
	{
		papplCopyString(driver_data->make_and_model, "Epson LX-350", sizeof(driver_data->make_and_model) - 1);

		driver_data->ppm = 8;
		driver_data->ppm = 1;

		driver_data->num_resolution = 3;
		driver_data->x_resolution[0] =  60;
		driver_data->y_resolution[0] =  72;
		driver_data->x_resolution[1] = 120;
		driver_data->y_resolution[1] = 144;
		driver_data->x_resolution[2] = 240;
		driver_data->y_resolution[2] = 216;
		driver_data->x_default = 240;
		driver_data->y_default = 216;

		driver_data->raster_types = PAPPL_PWG_RASTER_TYPE_SGRAY_8;
		driver_data->color_supported = PAPPL_COLOR_MODE_MONOCHROME;
		driver_data->color_default = PAPPL_COLOR_MODE_MONOCHROME;

		driver_data->num_media = 1;
		driver_data->media[0] = "na_letter_8.5x11in";

		driver_data->left_right = 300;
		driver_data->bottom_top = 850;

		driver_data->sides_supported = PAPPL_SIDES_ONE_SIDED;
		driver_data->sides_default = PAPPL_SIDES_ONE_SIDED;

		driver_data->num_source = 1;
		driver_data->source[0]  = "tray-1";

		driver_data->num_type = 1;
		driver_data->type[0] = "stationery";
	}

	for (int i = 0; i < driver_data->num_source; i ++)
	{
		pwg_media_t *pwg;

		papplCopyString(driver_data->media_ready[i].size_name, "na_letter_8.5x11in", sizeof(driver_data->media_ready[i].size_name));

		if ((pwg = pwgMediaForPWG(driver_data->media_ready[i].size_name)) != NULL)
		{
			driver_data->media_ready[i].bottom_margin = 1350;
			driver_data->media_ready[i].left_margin   = 300;
			driver_data->media_ready[i].right_margin  = 300;
			driver_data->media_ready[i].top_margin    = 850;
			driver_data->media_ready[i].size_width    = pwg->width;
			driver_data->media_ready[i].size_length   = pwg->length;
			papplCopyString(driver_data->media_ready[i].source, driver_data->source[i], sizeof(driver_data->media_ready[i].source));
			papplCopyString(driver_data->media_ready[i].type, driver_data->type[0],  sizeof(driver_data->media_ready[i].type));
		}
	}

	driver_data->media_default = driver_data->media_ready[0];

	return true;
}

const char *escp_autoadd(const char *device_info, const char *device_uri, const char *device_id, void *data)
{
	const char *ret = NULL;
	int num_did;
	cups_option_t *did;
	const char *cmd, *escp;

	num_did = papplDeviceParseID(device_id, &did);

	if ((cmd = cupsGetOption("COMMAND SET", num_did, did)) == NULL) cmd = cupsGetOption("CMD", num_did, did);

	if (cmd && (escp = strstr(cmd, "ESCP9")) != NULL && (escp[5] == ',' || !escp[5]))
	{
		ret = "epson_9pin_series";
	}

	cupsFreeOptions(num_did, did);

	return ret;
}

int main(int argc, char *argv[])
{
	return papplMainloop(
		argc, argv,
		"1.0",
		"Copyright &copy; 2025 Ada Hartley. Provided under the terms of the <a href=\"https://www.gnu.org/licenses/gpl-3.0.en.html\">GNU General Public License Version 3</a> or later.",
		(int)(sizeof(escp_drivers) / sizeof(escp_drivers[0])),
		escp_drivers, escp_autoadd, escp_callback,
		NULL, NULL,
		NULL,
		NULL,
		NULL
	);
}
