/*
 * WelcomeScreen.cpp - MANDELBROT's start screen. See WelcomeScreen.h.
 *
 * Part of MANDELBROT, a fork of LMMS. Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#include "WelcomeScreen.h"

#include <QCheckBox>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "ConfigManager.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "Song.h"
#include "embed.h"

namespace lmms::gui
{

namespace
{

QPushButton* bigButton(const QString& title, const QString& text, const QString& accent, QWidget* parent)
{
	auto b = new QPushButton(parent);
	b->setCursor(Qt::PointingHandCursor);
	b->setMinimumSize(230, 92);
	auto l = new QVBoxLayout(b);
	l->setContentsMargins(14, 10, 14, 10);
	auto t = new QLabel(QString("<span style='font-size:15pt; font-weight:600; color:%1'>%2</span>").arg(accent, title), b);
	auto d = new QLabel(QString("<span style='color:#9aa6bd'>%1</span>").arg(text), b);
	d->setWordWrap(true);
	t->setAttribute(Qt::WA_TransparentForMouseEvents);
	d->setAttribute(Qt::WA_TransparentForMouseEvents);
	l->addWidget(t);
	l->addWidget(d);
	l->addStretch();
	b->setStyleSheet(QString(
		"QPushButton { background:#161a23; border:1px solid #2b3140; border-radius:10px; text-align:left; }"
		"QPushButton:hover { background:#1c2230; border:1px solid %1; }"
		"QPushButton:pressed { background:#10131a; }").arg(accent));
	return b;
}

} // namespace

WelcomeScreen::WelcomeScreen(QWidget* parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Welcome to MANDELBROT"));
	setModal(true);
	setStyleSheet("QDialog { background:#0d0f15; } QLabel { color:#d7deea; } QCheckBox { color:#9aa6bd; }");

	auto root = new QVBoxLayout(this);
	root->setContentsMargins(0, 0, 0, 16);
	root->setSpacing(14);

	auto banner = new QLabel(this);
	banner->setPixmap(embed::getIconPixmap("splash"));
	banner->setAlignment(Qt::AlignCenter);
	banner->setStyleSheet("background:#06070c;");
	root->addWidget(banner);

	auto body = new QVBoxLayout;
	body->setContentsMargins(22, 0, 22, 0);
	body->setSpacing(10);
	root->addLayout(body);

	auto h1 = new QLabel(tr("<span style='font-size:12pt; color:#8fa0bd'>Start something</span>"), this);
	body->addWidget(h1);

	auto grid = new QGridLayout;
	grid->setSpacing(10);
	auto idm = bigButton(tr("Generative beat"), tr("IDM: drums, bass, chords, lead. The fractal writes every part."), "#28dcff", this);
	auto amb = bigButton(tr("Ambient"), tr("Drones, pads, bells and textures through the granular, spectral and resonator effects."), "#a98bff", this);
	auto brk = bigButton(tr("Breakbeat"), tr("A chopped jungle break plus sub, stabs and lead."), "#ff8a5b", this);
	auto blank = bigButton(tr("Blank project"), tr("A normal DAW project: write notes yourself, add the fractal to any track later."), "#d7deea", this);
	grid->addWidget(idm, 0, 0);
	grid->addWidget(amb, 0, 1);
	grid->addWidget(brk, 1, 0);
	grid->addWidget(blank, 1, 1);
	body->addLayout(grid);

	connect(idm, &QPushButton::clicked, this, [this] { startTemplate("MANDELBROT - IDM.mpt"); });
	connect(amb, &QPushButton::clicked, this, [this] { startTemplate("MANDELBROT - Ambient.mpt"); });
	connect(brk, &QPushButton::clicked, this, [this] { startTemplate("MANDELBROT - Breakbeat.mpt"); });
	connect(blank, &QPushButton::clicked, this, [this] { saveShowAtStartup(); accept(); });

	// recent projects
	const QStringList recent = ConfigManager::inst()->recentlyOpenedProjects();
	int shown = 0;
	for (const QString& f : recent)
	{
		if (shown >= 5 || !QFileInfo::exists(f) || f.endsWith(".mpt")) { continue; }
		if (shown == 0)
		{
			body->addWidget(new QLabel(tr("<span style='font-size:12pt; color:#8fa0bd'>Recent</span>"), this));
		}
		auto b = new QPushButton(QFileInfo(f).completeBaseName() + "   " + QFileInfo(f).absolutePath(), this);
		b->setCursor(Qt::PointingHandCursor);
		b->setStyleSheet("QPushButton { color:#d7deea; background:transparent; border:none; text-align:left; padding:3px; }"
			"QPushButton:hover { color:#28dcff; }");
		connect(b, &QPushButton::clicked, this, [this, f] { openRecent(f); });
		body->addWidget(b);
		++shown;
	}

	auto bottom = new QHBoxLayout;
	auto open = new QPushButton(tr("Open project..."), this);
	open->setStyleSheet("QPushButton { color:#d7deea; background:#1b1f2a; border:1px solid #2b3140; border-radius:6px; padding:6px 14px; }"
		"QPushButton:hover { border-color:#28dcff; }");
	connect(open, &QPushButton::clicked, this, [this] {
		saveShowAtStartup();
		accept();
		getGUI()->mainWindow()->openProject();
	});
	m_showAtStartup = new QCheckBox(tr("Show this at startup"), this);
	m_showAtStartup->setChecked(ConfigManager::inst()->value("mandelbrot", "nowelcome").toInt() == 0);
	bottom->addWidget(open);
	bottom->addStretch();
	bottom->addWidget(m_showAtStartup);
	body->addLayout(bottom);
	connect(this, &QDialog::rejected, this, [this] { saveShowAtStartup(); });
}

void WelcomeScreen::saveShowAtStartup()
{
	ConfigManager::inst()->setValue("mandelbrot", "nowelcome", m_showAtStartup->isChecked() ? "0" : "1");
}

void WelcomeScreen::startTemplate(const QString& name)
{
	saveShowAtStartup();
	accept();
	const QString file = ConfigManager::inst()->factoryTemplatesDir() + name;
	if (QFileInfo::exists(file))
	{
		Engine::getSong()->createNewProjectFromTemplate(file);
	}
}

void WelcomeScreen::openRecent(const QString& file)
{
	saveShowAtStartup();
	accept();
	Engine::getSong()->loadProject(file);
}

void WelcomeScreen::showAtStartup(QWidget* parent)
{
	if (ConfigManager::inst()->value("mandelbrot", "nowelcome").toInt() != 0) { return; }
	auto w = new WelcomeScreen(parent);
	w->setAttribute(Qt::WA_DeleteOnClose);
	w->open();
}

} // namespace lmms::gui
