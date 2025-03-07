#include "gl-app.h"

#include <string.h>
#include <stdio.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include "mupdf/helpers/pkcs7-openssl.h"

static void trace_field_value(pdf_annot *annot, const char *set_value)
{
	const char *get_value = pdf_annot_field_value(ctx, annot);
	trace_action("print('Set field %d:', repr(%q), repr(%q));\n", pdf_to_num(ctx, annot->obj), set_value, get_value);
}

static pdf_widget *sig_widget;
static char *sig_distinguished_name = NULL;
static pdf_signature_error sig_cert_error;
static pdf_signature_error sig_digest_error;
static int sig_valid_until;
static int sig_readonly;

static char cert_filename[PATH_MAX];
static struct input cert_password;

int do_sign(void)
{
	pdf_pkcs7_signer *signer = NULL;
	int ok = 1;

	fz_var(signer);

	fz_try(ctx)
	{
		trace_action("widget.sign(new PDFPKCS7Signer(%q, %q));\n", cert_filename, cert_password.text);
		signer = pkcs7_openssl_read_pfx(ctx, cert_filename, cert_password.text);
		pdf_sign_signature(ctx, sig_widget, signer, PDF_SIGNATURE_DEFAULT_APPEARANCE, NULL, NULL, NULL);
		ui_show_warning_dialog("Signed document successfully.");
	}
	fz_always(ctx)
	{
		pdf_drop_signer(ctx, signer);
	}
	fz_catch(ctx)
	{
		ui_show_warning_dialog("%s", fz_caught_message(ctx));
		ok = 0;
	}
	return ok;
}

static void do_clear_signature(void)
{
	fz_try(ctx)
	{
		trace_action("widget.clearSignature();\n");
		pdf_clear_signature(ctx, sig_widget);
		ui_show_warning_dialog("Signature cleared successfully.");
	}
	fz_catch(ctx)
		ui_show_warning_dialog("%s", fz_caught_message(ctx));
}

static int is_valid_certificate_and_password(void)
{
	fz_try(ctx)
	{
		pdf_pkcs7_signer *signer = pkcs7_openssl_read_pfx(ctx, cert_filename, cert_password.text);
		pdf_drop_signer(ctx, signer);
	}
	fz_catch(ctx)
		return 0;
	return 1;
}

static void cert_password_dialog(void)
{
	int is;
	ui_dialog_begin(ui.gridsize*16, (ui.gridsize+4)*3);
	{
		ui_layout(T, X, NW, ui.padsize, ui.padsize);
		ui_label("Password:");
		is = ui_input(&cert_password, 200, 1);

		ui_layout(B, X, NW, ui.padsize, ui.padsize);
		ui_panel_begin(0, ui.gridsize, 0, 0, 0);
		{
			ui_layout(R, NONE, S, 0, 0);
			if (ui_button("Cancel"))
				ui.dialog = NULL;
			ui_spacer();
			if (ui_button("Okay") || is == UI_INPUT_ACCEPT)
			{
				if (is_valid_certificate_and_password()) {
					ui.dialog = NULL;
					do_save_signed_pdf_file();
				} else {
					ui_show_warning_dialog("%s", fz_caught_message(ctx));
				}
			}
		}
		ui_panel_end();
	}
	ui_dialog_end();
}

static int cert_file_filter(const char *fn)
{
	return !!strstr(fn, ".pfx");
}

static void cert_file_dialog(void)
{
	if (ui_open_file(cert_filename, "Select a certificate file to sign with:"))
	{
		if (cert_filename[0] != 0)
		{
			ui_input_init(&cert_password, "");
			ui.focus = &cert_password;
			ui.dialog = cert_password_dialog;
		}
		else
			ui.dialog = NULL;
	}
}

static void sig_sign_dialog(void)
{
	const char *label = pdf_field_label(ctx, sig_widget->obj);

	ui_dialog_begin(ui.gridsize*16, (ui.gridsize+4)*3 + ui.lineheight*10);
	{
		ui_layout(T, X, NW, ui.padsize, ui.padsize);

		ui_label("%s", label);
		ui_spacer();

		ui_label("Would you like to sign this field?");

		ui_layout(B, X, NW, ui.padsize, ui.padsize);
		ui_panel_begin(0, ui.gridsize, 0, 0, 0);
		{
			ui_layout(R, NONE, S, 0, 0);
			if (ui_button("Cancel") || (!ui.focus && ui.key == KEY_ESCAPE))
				ui.dialog = NULL;
			ui_spacer();
			if (!(pdf_field_flags(ctx, sig_widget->obj) & PDF_FIELD_IS_READ_ONLY))
			{
				if (ui_button("Sign"))
				{
					fz_strlcpy(cert_filename, filename, sizeof cert_filename);
					ui_init_open_file(".", cert_file_filter);
					ui.dialog = cert_file_dialog;
				}
			}
		}
		ui_panel_end();
	}
	ui_dialog_end();
}

static void sig_verify_dialog(void)
{
	const char *label = pdf_field_label(ctx, sig_widget->obj);

	ui_dialog_begin(ui.gridsize*16, (ui.gridsize+4)*3 + ui.lineheight*10);
	{
		ui_layout(T, X, NW, ui.padsize, ui.padsize);

		ui_label("%s", label);
		ui_spacer();

		if (sig_readonly)
		{
			ui_label("Signature field is read-only.");
			ui_spacer();
		}

		ui_label("Distinguished name: %s.", sig_distinguished_name);
		ui_spacer();

		if (sig_cert_error)
			ui_label("Certificate error: %s", pdf_signature_error_description(sig_cert_error));
		else
			ui_label("Certificate is trusted.");

		ui_spacer();

		if (sig_digest_error)
			ui_label("Digest error: %s", pdf_signature_error_description(sig_digest_error));
		else if (sig_valid_until == 0)
			ui_label("The fields signed by this signature are unchanged.");
		else if (sig_valid_until == 1)
			ui_label("This signature was invalidated in the last update by the signed fields being changed.");
		else if (sig_valid_until == 2)
			ui_label("This signature was invalidated in the penultimate update by the signed fields being changed.");
		else
			ui_label("This signature was invalidated %d updates ago by the signed fields being changed.", sig_valid_until);

		ui_layout(B, X, NW, ui.padsize, ui.padsize);
		ui_panel_begin(0, ui.gridsize, 0, 0, 0);
		{
			ui_layout(L, NONE, S, 0, 0);
			if (!sig_readonly)
			{
				if (ui_button("Clear"))
				{
					ui.dialog = NULL;
					do_clear_signature();
				}
			}
			ui_layout(R, NONE, S, 0, 0);
			if (ui_button("Close") || (!ui.focus && ui.key == KEY_ESCAPE))
				ui.dialog = NULL;
		}
		ui_panel_end();
	}
	ui_dialog_end();
}

static void show_sig_dialog(pdf_widget *widget)
{
	fz_try(ctx)
	{
		sig_widget = widget;

		if (pdf_signature_is_signed(ctx, pdf, widget->obj))
		{
			pdf_pkcs7_verifier *verifier;
			pdf_pkcs7_distinguished_name *dn;

			sig_readonly = pdf_widget_is_readonly(ctx, widget);

			sig_valid_until = pdf_validate_signature(ctx, widget);

			verifier = pkcs7_openssl_new_verifier(ctx);

			sig_cert_error = pdf_check_certificate(ctx, verifier, pdf, widget->obj);
			sig_digest_error = pdf_check_digest(ctx, verifier, pdf, widget->obj);

			fz_free(ctx, sig_distinguished_name);
			dn = pdf_signature_get_signatory(ctx, verifier, pdf, widget->obj);
			if (dn)
				sig_distinguished_name = pdf_signature_format_distinguished_name(ctx, dn);
			else
				sig_distinguished_name = fz_strdup(ctx, "Signature information missing.");
			pdf_signature_drop_distinguished_name(ctx, dn);

			pdf_drop_verifier(ctx, verifier);

			ui.dialog = sig_verify_dialog;
		}
		else
		{
			ui.dialog = sig_sign_dialog;
		}
	}
	fz_catch(ctx)
		ui_show_warning_dialog("%s", fz_caught_message(ctx));
}

static pdf_widget *tx_widget;
static struct input tx_input;

static void tx_dialog(void)
{
	int ff = pdf_annot_field_flags(ctx, tx_widget);
	const char *label = pdf_annot_field_label(ctx, tx_widget);
	int tx_h = (ff & PDF_TX_FIELD_IS_MULTILINE) ? 10 : 1;
	int lbl_h = ui_break_lines((char*)label, NULL, 20, 394, NULL);
	int is;

	ui_dialog_begin(ui.gridsize*16, (ui.gridsize+4)*3 + ui.lineheight*(tx_h+lbl_h-2));
	{
		ui_layout(T, X, NW, ui.padsize, ui.padsize);
		ui_label("%s", label);
		is = ui_input(&tx_input, 200, tx_h);

		ui_layout(B, X, NW, ui.padsize, ui.padsize);
		ui_panel_begin(0, ui.gridsize, 0, 0, 0);
		{
			ui_layout(R, NONE, S, 0, 0);
			if (ui_button("Cancel") || (!ui.focus && ui.key == KEY_ESCAPE))
				ui.dialog = NULL;
			ui_spacer();
			if (ui_button("Okay") || is == UI_INPUT_ACCEPT)
			{
				trace_action("widget.setTextValue(%q);\n", tx_input.text);
				pdf_set_text_field_value(ctx, tx_widget, tx_input.text);
				trace_field_value(tx_widget, tx_input.text);
				ui.dialog = NULL;
			}
		}
		ui_panel_end();
	}
	ui_dialog_end();
}

void show_tx_dialog(pdf_widget *widget)
{
	ui_input_init(&tx_input, pdf_annot_field_value(ctx, widget));
	ui.focus = &tx_input;
	ui.dialog = tx_dialog;
	tx_widget = widget;
}

static pdf_widget *ch_widget;
static void ch_dialog(void)
{
	const char *label;
	const char *value;
	char **options;
	int n, choice;
	int label_h;

	label = pdf_field_label(ctx, ch_widget->obj);
	label_h = ui_break_lines((char*)label, NULL, 20, 394, NULL);
	n = pdf_choice_widget_options(ctx, ch_widget, 0, NULL);
	options = fz_malloc_array(ctx, n, char *);
	pdf_choice_widget_options(ctx, ch_widget, 0, (const char **)options);
	value = pdf_field_value(ctx, ch_widget->obj);

	ui_dialog_begin(ui.gridsize*16, (ui.gridsize+4)*3 + ui.lineheight*(label_h-1));
	{
		ui_layout(T, X, NW, ui.padsize, ui.padsize);

		ui_label("%s", label);
		choice = ui_select("Widget/Ch", value, (const char **)options, n);
		if (choice >= 0)
		{
			trace_action("widget.setChoiceValue(%q);\n", options[choice]);
			pdf_set_choice_field_value(ctx, ch_widget, options[choice]);
			trace_field_value(ch_widget, options[choice]);
		}

		ui_layout(B, X, NW, ui.padsize, ui.padsize);
		ui_panel_begin(0, ui.gridsize, 0, 0, 0);
		{
			ui_layout(R, NONE, S, 0, 0);
			if (ui_button("Cancel") || (!ui.focus && ui.key == KEY_ESCAPE))
				ui.dialog = NULL;
			ui_spacer();
			if (ui_button("Okay"))
			{
				ui.dialog = NULL;
			}
		}
		ui_panel_end();
	}
	ui_dialog_end();

	fz_free(ctx, options);
}

void do_widget_canvas(fz_irect canvas_area)
{
	pdf_widget *widget;
	fz_rect bounds;
	fz_irect area;
	int idx;

	if (!pdf)
		return;

	for (idx = 0, widget = pdf_first_widget(ctx, page); widget; ++idx, widget = pdf_next_widget(ctx, widget))
	{
		bounds = pdf_bound_widget(ctx, widget);
		bounds = fz_transform_rect(bounds, view_page_ctm);
		area = fz_irect_from_rect(bounds);

		if (ui_mouse_inside(canvas_area) && ui_mouse_inside(area))
		{
			if (pdf_annot_hot(ctx, widget))
			{
				trace_action("page.getWidgets()[%d].eventEnter();\n", idx);
				pdf_annot_event_enter(ctx, widget);
			}
			pdf_annot_set_hot(ctx, widget, 1);

			ui.hot = widget;
			if (!ui.active && ui.down)
			{
				ui.active = widget;
				trace_action("page.getWidgets()[%d].eventDown();\n", idx);
				pdf_annot_event_down(ctx, widget);
				if (selected_annot != widget)
				{
					if (selected_annot && pdf_annot_type(ctx, selected_annot) == PDF_ANNOT_WIDGET)
					{
						trace_action("widget.eventBlur();\n", idx);
						pdf_annot_event_blur(ctx, selected_annot);
					}
					trace_action("widget = page.getWidgets()[%d];\n", idx);
					selected_annot = widget;
					trace_action("widget.eventFocus();\n");
					pdf_annot_event_focus(ctx, widget);
				}
			}
		}
		else
		{
			if (widget->is_hot)
			{
				trace_action("page.getWidgets()[%d].eventExit();\n", idx);
				pdf_annot_event_exit(ctx, widget);
			}
			widget->is_hot = 0;
		}

		/* Set is_hot and is_active to select current appearance */
		pdf_annot_set_active(ctx, widget, (ui.active == widget && ui.down));

		if (showform)
		{
			glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
			glEnable(GL_BLEND);
			glColor4f(0, 0, 1, 0.1f);
			glRectf(area.x0, area.y0, area.x1, area.y1);
			glDisable(GL_BLEND);
		}

		if (ui.active == widget || (!ui.active && ui.hot == widget))
		{
			glLineStipple(1, 0xAAAA);
			glEnable(GL_LINE_STIPPLE);
			glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ZERO);
			glEnable(GL_BLEND);
			glColor4f(1, 1, 1, 1);
			glBegin(GL_LINE_LOOP);
			glVertex2f(area.x0-0.5f, area.y0-0.5f);
			glVertex2f(area.x1+0.5f, area.y0-0.5f);
			glVertex2f(area.x1+0.5f, area.y1+0.5f);
			glVertex2f(area.x0-0.5f, area.y1+0.5f);
			glEnd();
			glDisable(GL_BLEND);
			glDisable(GL_LINE_STIPPLE);
		}

		if (ui.hot == widget && ui.active == widget && !ui.down)
		{
			trace_action("widget.eventUp();\n");
			pdf_annot_event_up(ctx, widget);

			if (pdf_widget_type(ctx, widget) == PDF_WIDGET_TYPE_SIGNATURE)
			{
				show_sig_dialog(widget);
			}
			else
			{
				if (pdf_annot_field_flags(ctx, widget) & PDF_FIELD_IS_READ_ONLY)
					continue;

				switch (pdf_widget_type(ctx, widget))
				{
				default:
					break;
				case PDF_WIDGET_TYPE_CHECKBOX:
				case PDF_WIDGET_TYPE_RADIOBUTTON:
					trace_action("widget.toggle();\n");
					pdf_toggle_widget(ctx, widget);
					break;
				case PDF_WIDGET_TYPE_TEXT:
					show_tx_dialog(widget);
					break;
				case PDF_WIDGET_TYPE_COMBOBOX:
				case PDF_WIDGET_TYPE_LISTBOX:
					ui.dialog = ch_dialog;
					ch_widget = widget;
					break;
				}
			}
		}
	}
}
