/*
 * FractalWindow.cpp - MANDELBROT's fractal window.
 *
 * This file is part of MANDELBROT, a fork of LMMS - https://lmms.io
 * Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#include "FractalWindow.h"

#include <QComboBox>
#include <QDateTime>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

#include "ComboBox.h"
#include "Engine.h"
#include "FontHelper.h"
#include "FractalEngine.h"
#include "FractalGenerator.h"
#include "InstrumentTrack.h"
#include "Knob.h"
#include "LcdSpinBox.h"
#include "LedCheckBox.h"
#include "Song.h"
#include "embed.h"

namespace lmms::gui
{

// ============================================================================ FractalSetView
FractalSetView::FractalSetView(QWidget* parent) : QWidget(parent)
{
	setMinimumSize(320, 320);
	setCursor(Qt::CrossCursor);
	setToolTip(tr("Click: move the probe there and make it the anchor (orbit / box centre).  Wheel: zoom the view."));
	m_timer = new QTimer(this);
	connect(m_timer, SIGNAL(timeout()), this, SLOT(tick()));
	m_timer->start(100);
}

QPointF FractalSetView::toScreen(double x, double y) const
{
	const double s = m_viewScale / std::max(1, std::min(width(), height()));
	return {width() * 0.5 + (x - m_centreX) / s, height() * 0.5 + (y - m_centreY) / s};
}

void FractalSetView::render()
{
	const int w = std::max(1, width()), h = std::max(1, height());
	m_image = QImage(w, h, QImage::Format_RGB32);
	const double s = m_viewScale / std::max(1, std::min(w, h));
	const int maxIter = 220;
	for (int j = 0; j < h; ++j)
	{
		auto* line = reinterpret_cast<QRgb*>(m_image.scanLine(j));
		const double cy = m_centreY + (j - h * 0.5) * s;
		for (int i = 0; i < w; ++i)
		{
			const double cx = m_centreX + (i - w * 0.5) * s;
			const float e = fractal::math::pixel(cx, cy, maxIter);
			if (e >= 1.0f) { line[i] = qRgb(6, 8, 18); continue; }
			const float t = std::pow(e, 0.6f);
			const int r = static_cast<int>(10 + 40 * t + 180 * std::pow(t, 6.0f));
			const int g = static_cast<int>(14 + 150 * std::pow(t, 1.6f));
			const int b = static_cast<int>(40 + 215 * std::pow(t, 0.9f));
			line[i] = qRgb(std::min(r, 255), std::min(g, 255), std::min(b, 255));
		}
	}
	m_dirty = false;
}

void FractalSetView::tick()
{
	auto* fe = FractalEngine::instance();
	if (fe == nullptr || !isVisible()) { return; }
	const double px = fe->conductor().probeX.load(), py = fe->conductor().probeY.load();
	// keep the probe in the middle half of the view
	const QPointF p = toScreen(px, py);
	if (p.x() < width() * 0.25 || p.x() > width() * 0.75 || p.y() < height() * 0.25 || p.y() > height() * 0.75)
	{
		m_centreX = px; m_centreY = py; m_dirty = true;
	}
	if (m_dirty) { render(); }
	update();
}

void FractalSetView::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	if (m_image.size() != size()) { render(); }
	p.drawImage(0, 0, m_image);
	auto* fe = FractalEngine::instance();
	if (fe == nullptr) { return; }
	auto& c = fe->conductor();
	p.setRenderHint(QPainter::Antialiasing);
	// trail of the probe (the last 64 bars)
	const int tp = c.trailPos.load();
	QPainterPath path;
	bool first = true;
	for (int k = std::max(0, tp - 64); k < tp; ++k)
	{
		const QPointF q = toScreen(c.trailX[k % 64].load(), c.trailY[k % 64].load());
		if (first) { path.moveTo(q); first = false; } else { path.lineTo(q); }
	}
	p.setPen(QPen(QColor(255, 200, 80, 170), 1.5));
	p.drawPath(path);
	// the neighbourhood that is read (zoom)
	const QPointF probe = toScreen(c.probeX.load(), c.probeY.load());
	const double zoom = fe->m_zoomModel.value();
	const double s = m_viewScale / std::max(1, std::min(width(), height()));
	const double half = zoom / s * 0.5;
	p.setPen(QPen(QColor(255, 255, 255, 120), 1, Qt::DashLine));
	p.drawRect(QRectF(probe.x() - half, probe.y() - half, half * 2, half * 2));
	// anchor (orbit / box centre), the box, the bookmarks
	const QPointF anchor = toScreen(fe->anchorX(), fe->anchorY());
	const double box = fe->m_boxModel.value();
	if (box > 0.0)
	{
		const double hb = box / s;
		p.setPen(QPen(QColor(40, 220, 255, 150), 1, Qt::DotLine));
		p.drawRect(QRectF(anchor.x() - hb, anchor.y() - hb, hb * 2, hb * 2));
	}
	if (fe->m_probeModeModel.value() == fractal::GlobalSettings::Orbit)
	{
		const double r = fe->m_orbitRadiusModel.value() * zoom / s;
		p.setPen(QPen(QColor(40, 220, 255, 150), 1, Qt::DotLine));
		p.drawEllipse(anchor, r, r);
	}
	p.setPen(QPen(QColor(40, 220, 255), 1));
	p.drawLine(anchor + QPointF(-4, -4), anchor + QPointF(4, 4));
	p.drawLine(anchor + QPointF(-4, 4), anchor + QPointF(4, -4));
	for (int i = 0; i < 4; ++i)
	{
		if (!fe->bookmarkUsed(i)) { continue; }
		const QPointF bq = toScreen(fe->bookmarkX(i), fe->bookmarkY(i));
		p.setBrush(QColor(169, 139, 255));
		p.setPen(Qt::NoPen);
		p.drawEllipse(bq, 4, 4);
		p.setPen(QColor(220, 210, 255));
		p.drawText(bq + QPointF(6, -6), QString::number(i + 1));
		p.setBrush(Qt::NoBrush);
	}
	p.setPen(QPen(QColor(255, 90, 160), 2));
	p.drawEllipse(probe, 6, 6);
	p.drawLine(probe + QPointF(-11, 0), probe + QPointF(-7, 0));
	p.drawLine(probe + QPointF(7, 0), probe + QPointF(11, 0));
	p.drawLine(probe + QPointF(0, -11), probe + QPointF(0, -7));
	p.drawLine(probe + QPointF(0, 7), probe + QPointF(0, 11));
	p.setPen(Qt::white);
	p.drawText(8, height() - 8, QString("%1 %2%3i   view %4")
		.arg(c.probeX.load(), 0, 'f', 6).arg(c.probeY.load() < 0 ? "" : "+").arg(c.probeY.load(), 0, 'f', 6).arg(m_viewScale, 0, 'g', 3));
}

void FractalSetView::mousePressEvent(QMouseEvent* e)
{
	auto* fe = FractalEngine::instance();
	if (fe == nullptr) { return; }
	const double s = m_viewScale / std::max(1, std::min(width(), height()));
	const double x = m_centreX + (e->pos().x() - width() * 0.5) * s;
	const double y = m_centreY + (e->pos().y() - height() * 0.5) * s;
	fe->placeProbe(x, y);
	m_centreX = x; m_centreY = y; m_dirty = true;
	update();
}

void FractalSetView::wheelEvent(QWheelEvent* e)
{
	m_viewScale *= e->angleDelta().y() > 0 ? 0.8 : 1.25;
	m_viewScale = std::clamp(m_viewScale, 1e-6, 4.0);
	m_dirty = true;
	update();
}

// ============================================================================ FractalWindow
namespace
{
Knob* makeKnob(QWidget* parent, const QString& label, const QString& hint, FloatModel* m)
{
	auto k = new Knob(KnobType::Bright26, label, parent, Knob::LabelRendering::LegacyFixedFontSize);
	k->setHintText(hint, "");
	k->setModel(m);
	return k;
}

QLabel* makeSmallLabel(const QString& t)
{
	auto l = new QLabel(t);
	l->setFont(adjustedToPixelSize(l->font(), DEFAULT_FONT_SIZE));
	return l;
}
}

FractalWindow::FractalWindow() : QWidget()
{
	setWindowTitle(tr("Fractal engine"));
	setWindowIcon(embed::getIconPixmap("fractal_window"));
	auto* fe = FractalEngine::instance();

	auto root = new QHBoxLayout(this);
	root->setContentsMargins(6, 6, 6, 6);
	auto left = new QVBoxLayout();
	root->addLayout(left, 3);
	m_set = new FractalSetView(this);
	left->addWidget(m_set, 1);
	m_reading = new QLabel(this);
	m_reading->setWordWrap(true);
	m_reading->setFont(adjustedToPixelSize(m_reading->font(), DEFAULT_FONT_SIZE));
	m_reading->setMinimumHeight(110);
	m_reading->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	left->addWidget(m_reading);

	auto side = new QVBoxLayout();
	root->addLayout(side, 2);
	auto top = new QHBoxLayout();
	auto enabled = new LedCheckBox(tr("Engine on"), this);
	enabled->setModel(&fe->m_enabledModel);
	top->addWidget(enabled);
	auto freeze = new LedCheckBox(tr("Freeze probe"), this);
	freeze->setModel(&fe->m_freezeModel);
	top->addWidget(freeze);
	top->addStretch();
	auto dir = new QPushButton(tr("New direction"), this);
	dir->setToolTip(tr("Re-seeds the probe's walk: from the next phrase the music heads somewhere new"));
	connect(dir, SIGNAL(clicked()), this, SLOT(newDirection()));
	top->addWidget(dir);
	side->addLayout(top);

	auto tabs = new QTabWidget(this);
	tabs->addTab(buildProbeTab(), tr("Probe"));
	tabs->addTab(buildMusicTab(), tr("Music"));
	tabs->addTab(buildHistoryTab(), tr("History"));
	side->addWidget(tabs, 1);

	connect(fe, &FractalEngine::bookmarksChanged, this, [this] { refreshBookmarks(); });
	refreshBookmarks();

	m_timer = new QTimer(this);
	connect(m_timer, SIGNAL(timeout()), this, SLOT(refresh()));
	m_timer->start(250);
	refreshHistory();
}

namespace
{
ComboBox* makeCombo(QWidget* parent, ComboBoxModel* m)
{
	auto c = new ComboBox(parent);
	c->setFixedHeight(ComboBox::DEFAULT_HEIGHT);
	c->setModel(m);
	return c;
}

LcdSpinBox* makeLcd(QWidget* parent, int digits, const QString& label, const QString& tip, IntModel* m)
{
	auto l = new LcdSpinBox(digits, parent, label);
	l->setLabel(label);
	l->setModel(m);
	l->setToolTip(tip);
	return l;
}

QLabel* sectionLabel(const QString& t)
{
	auto l = new QLabel(QString("<b style='color:#28dcff'>%1</b>").arg(t));
	return l;
}
}

QWidget* FractalWindow::buildProbeTab()
{
	auto* fe = FractalEngine::instance();
	auto w = new QWidget(this);
	auto v = new QVBoxLayout(w);
	v->setSpacing(6);

	v->addWidget(sectionLabel(tr("MOVEMENT")));
	auto modeRow = new QHBoxLayout();
	modeRow->addWidget(makeSmallLabel(tr("Mode")));
	auto mode = makeCombo(w, &fe->m_probeModeModel);
	mode->setToolTip(tr("Edge walk: wanders, drawn to the boundary.  Free walk: wanders anywhere.\n"
		"Orbit: circles the anchor (the last point you clicked).  Path: glides between the bookmarks.\n"
		"Jumps: teleports to a new boundary point every few bars.  Locked: stays still."));
	modeRow->addWidget(mode, 1);
	v->addLayout(modeRow);

	auto move = new QHBoxLayout();
	move->addWidget(makeKnob(w, tr("SPEED"), tr("Probe speed:"), &fe->m_driftModel));
	move->addWidget(makeKnob(w, tr("TURN"), tr("How much the heading wanders:"), &fe->m_turnModel));
	move->addWidget(makeKnob(w, tr("EDGE"), tr("Pull towards the boundary (edge walk):"), &fe->m_edgePullModel));
	move->addWidget(makeKnob(w, tr("ORBIT"), tr("Orbit radius (in read radii):"), &fe->m_orbitRadiusModel));
	move->addWidget(makeKnob(w, tr("BOX"), tr("Boundary box half-size around the anchor (0 = no box):"), &fe->m_boxModel));
	v->addLayout(move);
	auto moveLcd = new QHBoxLayout();
	moveLcd->addWidget(makeLcd(w, 1, tr("STEPS"), tr("Probe moves per bar"), &fe->m_stepsModel));
	moveLcd->addWidget(makeLcd(w, 2, tr("JUMP"), tr("Bars between jumps (Jumps mode)"), &fe->m_jumpBarsModel));
	moveLcd->addStretch();
	v->addLayout(moveLcd);

	v->addWidget(sectionLabel(tr("READING")));
	auto read = new QHBoxLayout();
	read->addWidget(makeKnob(w, tr("RADIUS"), tr("Read radius (size of the neighbourhood that is read):"), &fe->m_zoomModel));
	read->addWidget(makeKnob(w, tr("BREATH"), tr("How much the read radius swells phrase to phrase:"), &fe->m_breatheModel));
	read->addWidget(makeLcd(w, 4, tr("DEPTH"), tr("Iteration depth: higher reads finer detail near the boundary"), &fe->m_maxIterModel));
	read->addWidget(makeLcd(w, 2, tr("RES"), tr("Read resolution: samples per side of the neighbourhood"), &fe->m_readGridModel));
	read->addStretch();
	v->addLayout(read);

	v->addWidget(sectionLabel(tr("BOOKMARKS")));
	auto bm = new QGridLayout();
	for (int i = 0; i < 4; ++i)
	{
		auto store = new QPushButton(tr("Store %1").arg(i + 1), w);
		store->setToolTip(tr("Remember the probe's position in slot %1").arg(i + 1));
		connect(store, &QPushButton::clicked, this, [i] { if (auto* e = FractalEngine::instance()) { e->storeBookmark(i); } });
		m_goBook[i] = new QPushButton(tr("Go %1").arg(i + 1), w);
		m_goBook[i]->setToolTip(tr("Jump to bookmark %1 (it becomes the anchor). Right-click: forget it").arg(i + 1));
		m_goBook[i]->setContextMenuPolicy(Qt::CustomContextMenu);
		connect(m_goBook[i], &QPushButton::clicked, this, [i] { if (auto* e = FractalEngine::instance()) { e->recallBookmark(i); } });
		connect(m_goBook[i], &QPushButton::customContextMenuRequested, this, [i] {
			if (auto* e = FractalEngine::instance()) { e->clearBookmark(i); }
		});
		bm->addWidget(store, 0, i);
		bm->addWidget(m_goBook[i], 1, i);
	}
	v->addLayout(bm);
	v->addWidget(makeSmallLabel(tr("Path mode glides through the stored bookmarks in order.")));
	v->addStretch();
	return w;
}

QWidget* FractalWindow::buildMusicTab()
{
	auto* fe = FractalEngine::instance();
	auto w = new QWidget(this);
	auto v = new QVBoxLayout(w);
	v->setSpacing(6);

	v->addWidget(sectionLabel(tr("KEY AND HARMONY")));
	auto key = new QGridLayout();
	key->addWidget(makeSmallLabel(tr("Key")), 0, 0);
	key->addWidget(makeCombo(w, &fe->m_rootModel), 1, 0);
	key->addWidget(makeSmallLabel(tr("Scale")), 0, 1);
	key->addWidget(makeCombo(w, &fe->m_scaleModel), 1, 1);
	key->addWidget(makeSmallLabel(tr("Harmonic rhythm")), 0, 2);
	key->addWidget(makeCombo(w, &fe->m_chordSpeedModel), 1, 2);
	auto fk = new LedCheckBox(tr("The set chooses the mode"), w);
	fk->setModel(&fe->m_fractalKeyModel);
	key->addWidget(fk, 2, 0, 1, 2);
	key->addWidget(makeCombo(w, &fe->m_paletteModel), 2, 2);
	v->addLayout(key);

	v->addWidget(sectionLabel(tr("RHYTHM AND PHRASING")));
	auto rk = new QHBoxLayout();
	rk->addWidget(makeKnob(w, tr("SUSTAIN"), tr("Sustain: 0 short and busy, 1 long, sparse, slow harmony:"), &fe->m_sustainModel));
	rk->addWidget(makeKnob(w, tr("SYNC"), tr("Syncopation: how far parts move off the beat:"), &fe->m_syncopationModel));
	rk->addWidget(makeKnob(w, tr("SWING"), tr("Swing:"), &fe->m_swingModel));
	rk->addWidget(makeKnob(w, tr("CPLX"), tr("Complexity:"), &fe->m_complexityModel));
	rk->addWidget(makeKnob(w, tr("FRACT"), tr("Fractal control (how much the set decides the rules):"), &fe->m_fractalControlModel));
	v->addLayout(rk);
	auto rc = new QGridLayout();
	rc->addWidget(makeSmallLabel(tr("Grid")), 0, 0);
	rc->addWidget(makeCombo(w, &fe->m_gridModel), 1, 0);
	rc->addWidget(makeSmallLabel(tr("Phrase length")), 0, 1);
	rc->addWidget(makeCombo(w, &fe->m_phraseBarsModel), 1, 1);
	v->addLayout(rc);

	v->addWidget(sectionLabel(tr("NOVELTY")));
	auto nv = new QHBoxLayout();
	nv->addWidget(makeKnob(w, tr("NEW"), tr("Novelty (how hard old material is avoided):"), &fe->m_noveltyModel));
	auto nm = new QVBoxLayout();
	nm->addWidget(makeSmallLabel(tr("Avoid repeating")));
	nm->addWidget(makeCombo(w, &fe->m_noveltyModeModel));
	nv->addLayout(nm, 1);
	v->addLayout(nv);
	v->addStretch();
	return w;
}

QWidget* FractalWindow::buildHistoryTab()
{
	auto w = new QWidget(this);
	auto v = new QVBoxLayout(w);
	m_historyCount = makeSmallLabel("");
	v->addWidget(m_historyCount);
	m_history = new QTreeWidget(w);
	m_history->setColumnCount(5);
	m_history->setHeaderLabels({tr("When"), tr("Track"), tr("Role"), tr("Bars"), tr("Notes")});
	m_history->setRootIsDecorated(false);
	m_history->setSelectionMode(QAbstractItemView::SingleSelection);
	m_history->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
	v->addWidget(m_history, 1);
	auto histButtons = new QHBoxLayout();
	m_targetTrack = new QComboBox(w);
	m_targetTrack->setToolTip(tr("Track that receives the phrase"));
	histButtons->addWidget(m_targetTrack, 1);
	auto send = new QPushButton(tr("To clip"), w);
	send->setToolTip(tr("Write the selected phrase into a MIDI clip on the chosen track, at the play head"));
	connect(send, SIGNAL(clicked()), this, SLOT(sendToTrack()));
	histButtons->addWidget(send);
	auto fav = new QPushButton(tr("Favourite"), w);
	connect(fav, SIGNAL(clicked()), this, SLOT(toggleFavourite()));
	histButtons->addWidget(fav);
	auto clr = new QPushButton(tr("Clear"), w);
	connect(clr, SIGNAL(clicked()), this, SLOT(clearHistory()));
	histButtons->addWidget(clr);
	v->addLayout(histButtons);
	return w;
}

void FractalWindow::refreshBookmarks()
{
	auto* fe = FractalEngine::instance();
	if (fe == nullptr) { return; }
	for (int i = 0; i < 4; ++i) { m_goBook[i]->setEnabled(fe->bookmarkUsed(i)); }
}

void FractalWindow::refresh()
{
	auto* fe = FractalEngine::instance();
	if (fe == nullptr || !isVisible()) { return; }
	auto& c = fe->conductor();
	static const char* forms[] = {"A (new motif)", "A' (variation)", "B (contrast)", "A'' (return)", "C (new contrast)", "B' (variation)"};
	QString t;
	if (!c.running())
	{
		t = tr("Stopped. Press play in the Song Editor: every track with a fractal role plays live.\n\n");
	}
	else
	{
		t = tr("Phrase %1  |  bar %2 of %3  |  form %4\nKey %5 %6  |  chord on degree %7\n")
			.arg(c.phraseCounter.load()).arg(c.currentBar.load() + 1).arg(c.currentBars.load())
			.arg(forms[fractal::clampv(c.currentForm.load(), 0, 5)])
			.arg(fractal::scales::rootName(c.currentKeyRoot.load())).arg(fractal::scales::name(c.currentScale.load()))
			.arg(c.currentChordDegree.load() + 1);
	}
	t += tr("\nWhat the set reads here:\n"
		"  escape time %1  -> register, harmonic rhythm\n"
		"  period %2  -> riff length, form, kick pattern\n"
		"  boundary %3  -> ornaments, percussion\n"
		"  chaos %4  -> chord colour, break chopping\n"
		"  entropy %5  -> chord speed, texture density\n"
		"  roughness %6  -> hi-hat rolls, syncopation\n"
		"Groove likeness to earlier grooves: %7%")
		.arg(c.currentEscape.load(), 0, 'f', 2).arg(c.currentPeriod.load() > 0 ? QString::number(c.currentPeriod.load()) : QString("-"))
		.arg(c.currentBoundary.load(), 0, 'f', 2).arg(c.currentChaos.load(), 0, 'f', 2)
		.arg(c.currentEntropy.load(), 0, 'f', 2).arg(c.currentRoughness.load(), 0, 'f', 2)
		.arg(static_cast<int>(c.currentDrumSimilarity.load() * 100.0f));
	m_reading->setText(t);
	if (c.phraseCounter.load() != m_lastPhrase)
	{
		m_lastPhrase = c.phraseCounter.load();
		refreshHistory();
	}
}

void FractalWindow::refreshHistory()
{
	auto* fe = FractalEngine::instance();
	if (fe == nullptr) { return; }
	const auto recs = fe->history().recent(400);
	m_history->clear();
	for (const auto& r : recs)
	{
		if (r.role >= static_cast<int>(fractal::Role::NumRoles)) { continue; } // grooves are internal
		auto item = new QTreeWidgetItem(m_history);
		const QString when = QDateTime::fromSecsSinceEpoch(r.time).toString("MMM d hh:mm:ss");
		item->setText(0, (r.favourite ? QString::fromUtf8("★ ") : QString()) + when);
		item->setText(1, QString::fromStdString(r.track));
		item->setText(2, fractal::roleName(r.role));
		item->setText(3, QString::number(r.bars));
		item->setText(4, QString::number(r.notes.size()));
		item->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(r.id));
		item->setToolTip(0, tr("From %1 %2i").arg(r.cx, 0, 'f', 6).arg(r.cy, 0, 'f', 6));
	}
	m_historyCount->setText(tr("%1 phrases remembered").arg(fe->history().size()));
	// tracks that can receive a phrase
	const QString cur = m_targetTrack->currentText();
	m_targetTrack->clear();
	for (auto* track : Engine::getSong()->tracks())
	{
		if (dynamic_cast<InstrumentTrack*>(track) != nullptr) { m_targetTrack->addItem(track->name()); }
	}
	const int idx = m_targetTrack->findText(cur);
	if (idx >= 0) { m_targetTrack->setCurrentIndex(idx); }
}

void FractalWindow::sendToTrack()
{
	auto* fe = FractalEngine::instance();
	auto* item = m_history->currentItem();
	if (fe == nullptr || item == nullptr) { return; }
	fractal::PhraseRecord rec;
	if (!fe->history().get(item->data(0, Qt::UserRole).toULongLong(), rec)) { return; }
	for (auto* track : Engine::getSong()->tracks())
	{
		auto* it = dynamic_cast<InstrumentTrack*>(track);
		if (it != nullptr && it->name() == m_targetTrack->currentText())
		{
			it->fractalGenerator()->writeClip(rec.notes, rec.bars,
				QString("%1 #%2").arg(fractal::roleName(rec.role)).arg(rec.id));
			return;
		}
	}
}

void FractalWindow::toggleFavourite()
{
	auto* fe = FractalEngine::instance();
	auto* item = m_history->currentItem();
	if (fe == nullptr || item == nullptr) { return; }
	const auto id = item->data(0, Qt::UserRole).toULongLong();
	fractal::PhraseRecord rec;
	if (fe->history().get(id, rec)) { fe->history().setFavourite(id, !rec.favourite); }
	refreshHistory();
}

void FractalWindow::clearHistory()
{
	auto* fe = FractalEngine::instance();
	if (fe == nullptr) { return; }
	if (QMessageBox::question(this, tr("Clear phrase history"),
		tr("Forget every phrase MANDELBROT has played? Old material may then come back.")) != QMessageBox::Yes) { return; }
	fe->history().clear();
	fe->saveHistory();
	refreshHistory();
}

void FractalWindow::newDirection()
{
	auto* fe = FractalEngine::instance();
	if (fe == nullptr) { return; }
	fe->conductor().reseed(static_cast<uint32_t>(QDateTime::currentMSecsSinceEpoch()));
}

} // namespace lmms::gui
