/***********************************************************************
 *
 * Copyright (C) 2008 Graeme Gott <graeme@gottcode.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ***********************************************************************/

#include "window.h"

#include "preferences.h"

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QTextStream>
#include <QTime>
#include <QTimer>
#include <QToolBar>

/*****************************************************************************/

namespace {
	// Window list to share fullscreen status
	QList<Window*> windows;

	// Make actions work even when toolbar is hidden
	void addShortcut(QAction* action, const QKeySequence& key) {
		QShortcut* shortcut = new QShortcut(key, action->parentWidget()->window());
		QObject::connect(shortcut, SIGNAL(activated()), action, SLOT(trigger()));
	}

	// Text block word count
	class WordCountData : public QTextBlockUserData {
	public:
		WordCountData() : m_words(0) {
		}

		int count() const {
			return m_words;
		}

		int update(const QString& text) {
			m_words = text.split(QRegExp("\\s+"), QString::SkipEmptyParts).count();
			return m_words;
		}

	private:
		int m_words;
	};
}

/*****************************************************************************/

Window::Window(Preferences* preferences)
: m_background_position(0), m_auto_save(false), m_preferences(preferences) {
	windows.append(this);

	setWindowTitle("FocusWriter");
	setWindowIcon(QIcon(":/focuswriter.png"));
	setMouseTracking(true);
	installEventFilter(this);

	m_hide_timer = new QTimer(this);
	m_hide_timer->setInterval(5000);
	m_hide_timer->setSingleShot(true);
	connect(m_hide_timer, SIGNAL(timeout()), this, SLOT(hideMouse()));

	// Set up details
	m_details = new QWidget(this);
	m_details->setPalette(QApplication::palette());
	m_details->setAutoFillBackground(true);
	m_details->setVisible(false);

	m_filename_label = new QLabel(m_details);
	m_filename_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

	m_wordcount_label = new QLabel(tr("0 words"), m_details);
	m_wordcount_label->setAlignment(Qt::AlignCenter);

	m_clock_label = new QLabel(m_details);
	m_clock_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	updateClock();

	// Set up clock
	m_clock_timer = new QTimer(this);
	m_clock_timer->setInterval(60000);
	int delay = (60 - QTime::currentTime().second()) * 1000;
	QTimer::singleShot(delay, m_clock_timer, SLOT(start()));
	QTimer::singleShot(delay, this, SLOT(updateClock()));
	connect(m_clock_timer, SIGNAL(timeout()), this, SLOT(updateClock()));

	// Set up text area
	m_text = new QPlainTextEdit(this);
	m_text->setCenterOnScroll(true);
	m_text->setTabStopWidth(50);
	m_text->setMinimumHeight(500);
	m_text->setFrameStyle(QFrame::NoFrame);
	m_text->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_text->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_text->viewport()->setMouseTracking(true);
	m_text->viewport()->installEventFilter(this);
	connect(m_text, SIGNAL(textChanged()), m_text, SLOT(centerCursor()));
	connect(m_text->document(), SIGNAL(contentsChange(int,int,int)), this, SLOT(updateWordCount(int,int,int)));

	m_scrollbar = m_text->verticalScrollBar();
	m_scrollbar->setVisible(false);

	// Set up toolbar
	m_toolbar = new QToolBar(this);
	m_toolbar->setPalette(QApplication::palette());
	m_toolbar->setAutoFillBackground(true);
	m_toolbar->setIconSize(QSize(22,22));
	m_toolbar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
	m_toolbar->setPalette(QApplication::palette());
	m_toolbar->setVisible(false);
	connect(m_toolbar, SIGNAL(actionTriggered(QAction*)), m_toolbar, SLOT(hide()));

	QAction* action;

	action = m_toolbar->addAction(QIcon(":/document-new.png"), tr("New"), this, SLOT(newClicked()));
	addShortcut(action, Qt::CTRL + Qt::Key_N);

	action = m_toolbar->addAction(QIcon(":/document-open.png"), tr("Open"), this, SLOT(openClicked()));
	addShortcut(action, Qt::CTRL + Qt::Key_O);

	action = m_toolbar->addAction(QIcon(":/document-save.png"), tr("Save"), this, SLOT(saveClicked()));
	addShortcut(action, Qt::CTRL + Qt::Key_S);
	action->setEnabled(false);
	connect(m_text, SIGNAL(modificationChanged(bool)), action, SLOT(setEnabled(bool)));

	m_toolbar->addAction(QIcon(":/document-save-as.png"), tr("Rename"), this, SLOT(renameClicked()));

	action = m_toolbar->addAction(QIcon(":/window-close.png"), tr("Close"), this, SLOT(close()));
	addShortcut(action, Qt::CTRL + Qt::Key_W);

	m_toolbar->addSeparator();

	action = m_toolbar->addAction(QIcon(":/document-print.png"), tr("Print"), this, SLOT(printClicked()));
	addShortcut(action, Qt::CTRL + Qt::Key_P);

	m_fullscreen_action = m_toolbar->addAction(QIcon(":/view-fullscreen.png"), tr("Fullscreen"));
	addShortcut(m_fullscreen_action, Qt::Key_Escape);
	m_fullscreen_action->setCheckable(true);
	connect(m_fullscreen_action, SIGNAL(triggered(bool)), this, SLOT(setFullscreen(bool)));

	m_toolbar->addAction(QIcon(":/preferences-other.png"), tr("Preferences"), m_preferences, SLOT(exec()));

	m_toolbar->addAction(QIcon(":/help-about.png"), tr("About"), this, SLOT(aboutClicked()));

	m_toolbar->addSeparator();

	action = m_toolbar->addAction(QIcon(":/application-exit.png"), tr("Quit"), qApp, SLOT(closeAllWindows()));
	addShortcut(action, Qt::CTRL + Qt::Key_Q);

	// Lay out details
	QGridLayout* details_layout = new QGridLayout(m_details);
	details_layout->setSpacing(12);
	details_layout->setMargin(0);
	details_layout->setColumnMinimumWidth(0, 6);
	details_layout->setColumnStretch(1, 1);
	details_layout->setColumnStretch(2, 1);
	details_layout->setColumnStretch(3, 1);
	details_layout->setColumnMinimumWidth(4, 6);
	details_layout->addWidget(m_filename_label, 0, 1);
	details_layout->addWidget(m_wordcount_label, 0, 2);
	details_layout->addWidget(m_clock_label, 0, 3);

	// Lay out window
	m_margin = m_toolbar->sizeHint().height();
	QGridLayout* layout = new QGridLayout(this);
	layout->setSpacing(0);
	layout->setMargin(0);
	layout->setColumnStretch(0, 1);
	layout->setColumnStretch(2, 1);
	layout->setColumnMinimumWidth(0, m_margin);
	layout->setColumnMinimumWidth(2, m_margin);
	layout->setRowMinimumHeight(0, m_margin);
	layout->setRowMinimumHeight(2, m_margin);
	layout->addWidget(m_toolbar, 0, 0, 1, 3);
	layout->addWidget(m_text, 1, 1);
	layout->addWidget(m_scrollbar, 1, 2, Qt::AlignRight);
	layout->addWidget(m_details, 2, 0, 1, 3, Qt::AlignBottom);

	// Connect to preferences dialog
	connect(m_preferences, SIGNAL(colorsChanged(const QColor&, const QColor&)), this, SLOT(updateColors(const QColor&, const QColor&)));
	connect(m_preferences, SIGNAL(backgroundChanged(const QColor&, const QImage&, int)), this, SLOT(updateBackground(const QColor&, const QImage&, int)));
	connect(m_preferences, SIGNAL(fontChanged(const QFont&)), this, SLOT(updateFont(const QFont&)));
	connect(m_preferences, SIGNAL(widthChanged(int)), this, SLOT(updateWidth(int)));
	connect(m_preferences, SIGNAL(autoSaveChanged(bool)), this, SLOT(updateAutoSave(bool)));
	m_preferences->emitSettings();

	// Load windowed size
	restoreGeometry(QSettings().value("Window/Geometry").toByteArray());

	// Determine if it should be fullscreen or not
	bool fullscreen = QSettings().value("Window/Fullscreen", true).toBool();
	m_fullscreen_action->setChecked(fullscreen);
	setFullscreen(fullscreen);
}

/*****************************************************************************/

void Window::open(const QString& filename) {
	QFile file(filename);
	if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		m_filename = filename;
		m_filename_label->setText(m_filename.section('/', -1));

		QTextStream stream(&file);
		m_text->setPlainText(stream.readAll());
		m_text->moveCursor(QTextCursor::End);
		m_text->document()->setModified(false);
	}
	m_text->centerCursor();
}

/*****************************************************************************/

bool Window::eventFilter(QObject* watched, QEvent* event) {
	if (event->type() == QEvent::MouseMove) {
		m_text->viewport()->setCursor(Qt::IBeamCursor);
		m_text->parentWidget()->unsetCursor();
		if (watched == m_text->viewport() || watched == m_text->parentWidget()) {
			m_hide_timer->start();
		}
		const QPoint& point = mapFromGlobal(static_cast<QMouseEvent*>(event)->globalPos());
		m_toolbar->setVisible(point.y() <= m_margin);
		m_details->setVisible(point.y() >= (height() - m_margin));
		m_scrollbar->setVisible(point.x() >= (width() - m_margin) && !m_toolbar->isVisible() && !m_details->isVisible());
	} else if (event->type() == QEvent::Wheel) {
		if (watched == m_text->parentWidget()) {
			int delta = static_cast<QWheelEvent*>(event)->delta();
			if ( (delta > 0 && m_scrollbar->value() > m_scrollbar->minimum()) || (delta < 0 && m_scrollbar->value() < m_scrollbar->maximum()) ) {
				qApp->sendEvent(m_scrollbar, event);
			}
		}
	}
	return QWidget::eventFilter(watched, event);
}

/*****************************************************************************/

bool Window::event(QEvent* event) {
	if (event->type() == QEvent::WindowActivate) {
		m_preferences->setParent(this, Qt::Dialog);
	} else if (event->type() == QEvent::WindowBlocked) {
		m_toolbar->hide();
	}
	return QWidget::event(event);
}

/*****************************************************************************/

void Window::closeEvent(QCloseEvent* event) {
	if (m_auto_save) {
		saveClicked();
	} else if (m_text->document()->isModified()) {
		switch (QMessageBox::question(this, tr("Question"), tr("Save changes?"), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel)) {
		case QMessageBox::Save:
			saveClicked();
		case QMessageBox::Discard:
			event->accept();
			windows.removeAll(this);
			break;
		case QMessageBox::Cancel:
		default:
			event->ignore();
			break;
		}
		return;
	}

	windows.removeAll(this);
	QWidget::closeEvent(event);
}

/*****************************************************************************/

void Window::paintEvent(QPaintEvent*) {
	if (m_background_position > 1) {
		QPainter painter(this);
		QSize s = (size() - m_background_scaled.size()) / 2;
		painter.drawImage(s.width(), s.height(), m_background_scaled);
	}
}

/*****************************************************************************/

void Window::resizeEvent(QResizeEvent* event) {
	if (isActiveWindow() && !isFullScreen()) {
		QSettings().setValue("Window/Geometry", saveGeometry());
	}
	scaleBackground();
	QWidget::resizeEvent(event);
}

/*****************************************************************************/

void Window::newClicked() {
	new Window(m_preferences);
}

/*****************************************************************************/

void Window::openClicked() {
	QString filename = QFileDialog::getOpenFileName(this, QString(), QString(), tr("Plain Text (*.txt)"));
	if (!filename.isEmpty()) {
		Window* window = (m_filename.isEmpty() && !m_text->document()->isModified()) ? this : new Window(m_preferences);
		window->open(filename);
	}
}

/*****************************************************************************/

void Window::saveClicked() {
	// Don't save unless modified
	if (!m_text->document()->isModified()) {
		return;
	}
	m_text->document()->setModified(false);

	// Fetch filename of new files
	if (m_filename.isEmpty()) {
		int max = 0;
		QRegExp regex("^Document (\\d+).txt$");
		foreach (const QString& file, QDir().entryList()) {
			if (regex.exactMatch(file)) {
				int val = regex.cap(1).toInt();
				max = (val > max) ? val : max;
			}
		}
		m_filename = QString("Document %1.txt").arg(++max);
		m_filename_label->setText(m_filename.section('/', -1));
	}

	// Write file to disk
	QFile file(m_filename);
	if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		QTextStream stream(&file);
		stream << m_text->toPlainText();
	}

	// Store as active file
	QSettings().setValue("Save/Current", m_filename);
}

/*****************************************************************************/

void Window::renameClicked() {
	if (m_filename.isEmpty()) {
		return;
	}

	QString filename = QFileDialog::getSaveFileName(this, tr("Rename"), m_filename, tr("Plain Text (*.txt)"));
	if (!filename.isEmpty()) {
		if (!filename.endsWith(".txt")) {
			filename.append(".txt");
		}
		QFile::remove(filename);
		QFile::rename(m_filename, filename);
		m_filename = filename;
		m_filename_label->setText(m_filename.section('/', -1));
	}
}

/*****************************************************************************/

void Window::printClicked() {
	QPrinter printer;
	printer.setPageSize(QPrinter::Letter);
	printer.setPageMargins(0.5, 0.5, 0.5, 0.5, QPrinter::Inch);
	QPrintDialog dialog(&printer, this);
	if (dialog.exec() == QDialog::Accepted) {
		m_text->print(&printer);
	}
}

/*****************************************************************************/

void Window::aboutClicked() {
	QMessageBox::about(this, tr("About FocusWriter"), tr(
		"<center>"
		"<big><b>FocusWriter 1.0.0</b></big><br/>"
		"A simple fullscreen word processor<br/>"
		"<small>Copyright &copy; 2008 Graeme Gott</small><br/><br/>"
		"Toolbar icons are from <a href=\"http://www.oxygen-icons.org/\">Oyxgen</a>"
		"</center>"
	));
}

/*****************************************************************************/

void Window::setFullscreen(bool fullscreen) {
	QSettings().setValue("Window/Fullscreen", fullscreen);
	foreach (Window* window, windows) {
		if (fullscreen) {
			window->showFullScreen();
		} else {
			window->showNormal();
		}
		window->m_fullscreen_action->setChecked(fullscreen);
	}
}

/*****************************************************************************/

void Window::updateColors(const QColor& text, const QColor& page) {
	QPalette p = m_text->palette();
	p.setColor(QPalette::Base, page);
	p.setColor(QPalette::Text, text);
	p.setColor(QPalette::Highlight, text);
	p.setColor(QPalette::HighlightedText, page.rgb());
	m_text->setPalette(p);
}

/*****************************************************************************/

void Window::updateBackground(const QColor& color, const QImage& image, int position) {
	m_background = image;
	m_background_position = position;

	QPalette p = palette();
	if (m_background_position != 1) {
		p.setColor(QPalette::Window, color.rgb());
	} else {
		p.setBrush(QPalette::Window, m_background);
	}
	setPalette(p);

	scaleBackground();
	update();
}

/*****************************************************************************/

void Window::updateFont(const QFont& font) {
	m_text->setFont(font);
}

/*****************************************************************************/

void Window::updateWidth(int width) {
	m_text->setFixedWidth(width);
}

/*****************************************************************************/

void Window::updateAutoSave(bool enabled) {
	m_auto_save = enabled;
	if (enabled) {
		connect(m_clock_timer, SIGNAL(timeout()), this, SLOT(saveClicked()));
	} else {
		m_clock_timer->disconnect(SIGNAL(timeout()), this, SLOT(saveClicked()));
	}
}

/*****************************************************************************/

void Window::hideMouse() {
	QWidget* widget = QApplication::widgetAt(QCursor::pos());
	if (widget == m_text->viewport() || widget == m_text->parentWidget()) {
		m_text->viewport()->setCursor(Qt::BlankCursor);
		m_text->parentWidget()->setCursor(Qt::BlankCursor);
	}
}

/*****************************************************************************/

void Window::updateWordCount(int position, int removed, int added) {
	QTextBlock begin = m_text->document()->findBlock(position - removed);
	QTextBlock end = m_text->document()->findBlock(position + added);
	if (end.isValid()) {
		end = end.next();
	}
	for (QTextBlock i = begin; i != end; i = i.next()) {
		if (i.userData()) {
			static_cast<WordCountData*>(i.userData())->update(i.text());
		}
	}

	int words = 0;
	for (QTextBlock i = m_text->document()->begin(); i != m_text->document()->end(); i = i.next()) {
		if (i.userData()) {
			words += static_cast<WordCountData*>(i.userData())->count();
		} else {
			WordCountData* data = new WordCountData;
			words += data->update(i.text());
			i.setUserData(data);
		}
	}
	m_wordcount_label->setText(words != 1 ? tr("%L1 words").arg(words) : tr("1 word"));
}

/*****************************************************************************/

void Window::updateClock() {
	m_clock_label->setText(QTime::currentTime().toString("h:mm A"));
}

/*****************************************************************************/

void Window::scaleBackground() {
	switch (m_background_position) {
	case 2:
		m_background_scaled = m_background;
		break;
	case 3:
		m_background_scaled = m_background.scaled(size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		break;
	case 4:
		m_background_scaled = m_background.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
		break;
	case 5:
		m_background_scaled = m_background.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
		break;
	default:
		break;
	}
}

/*****************************************************************************/