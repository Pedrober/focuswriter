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

#ifndef WINDOW_H
#define WINDOW_H

#include <QWidget>
class QAction;
class QGridLayout;
class QLabel;
class QScrollBar;
class QPlainTextEdit;
class QTextBlock;
class QTimer;
class QToolBar;
class Preferences;

class Window : public QWidget {
	Q_OBJECT
public:
	Window(Preferences* preferences);

	void open(const QString& filename);

	virtual bool eventFilter(QObject* watched, QEvent* event);

protected:
	virtual bool event(QEvent* event);
	virtual void closeEvent(QCloseEvent* event);
	virtual void paintEvent(QPaintEvent* event);
	virtual void resizeEvent(QResizeEvent* event);

private slots:
	void newClicked();
	void openClicked();
	void saveClicked();
	void renameClicked();
	void printClicked();
	void aboutClicked();
	void setFullscreen(bool fullscreen);
	void hideMouse();
	void updateColors(const QColor& text, const QColor& page);
	void updateBackground(const QColor& color, const QImage& image, int position);
	void updateFont(const QFont& font);
	void updateWidth(int width);
	void updateAutoSave(bool enabled);
	void updateWordCount(int position, int removed, int added);
	void updateClock();

private:
	void scaleBackground();

private:
	QString m_filename;
	QImage m_background;
	QImage m_background_scaled;
	int m_background_position;
	QToolBar* m_toolbar;
	QAction* m_fullscreen_action;
	QPlainTextEdit* m_text;
	QScrollBar* m_scrollbar;
	QWidget* m_details;
	QLabel* m_filename_label;
	QLabel* m_wordcount_label;
	QLabel* m_clock_label;
	int m_margin;
	QTimer* m_clock_timer;
	QTimer* m_hide_timer;
	bool m_auto_save;
	Preferences* m_preferences;
};

#endif