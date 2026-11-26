/*
 * WelcomeScreen.h - MANDELBROT's start screen: start a generative project (IDM, Ambient,
 *                   Breakbeat), a blank project for normal use, or open a recent one.
 *
 * Part of MANDELBROT, a fork of LMMS. Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#ifndef LMMS_GUI_WELCOME_SCREEN_H
#define LMMS_GUI_WELCOME_SCREEN_H

#include <QDialog>

class QCheckBox;

namespace lmms::gui
{

class WelcomeScreen : public QDialog
{
	Q_OBJECT
public:
	explicit WelcomeScreen(QWidget* parent);

	//! Shows the start screen unless the user turned it off
	static void showAtStartup(QWidget* parent);

private:
	void startTemplate(const QString& name);
	void openRecent(const QString& file);
	void saveShowAtStartup();

	QCheckBox* m_showAtStartup;
};

} // namespace lmms::gui

#endif // LMMS_GUI_WELCOME_SCREEN_H
