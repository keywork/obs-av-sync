/*
obs-av-sync — Automatic multi-camera AV sync for OBS Studio
Copyright (C) 2026 Sean Mahoney <sean@mahoney.xyz>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <QDockWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QTimer>
#include <QColor>
#include <QBrush>
#include <vector>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include "av_sync_dock.h"
#include "av_sync_filter.h"

struct filter_snapshot {
	std::string source_name;
	int status;
	bool enabled;
	float offset_ms;
	float confidence;
	std::string reference;
};

static void enum_callback(struct av_sync_filter_data *inst, void *userdata)
{
	auto *vec = static_cast<std::vector<filter_snapshot> *>(userdata);
	filter_snapshot snap;
	const char *name = av_sync_filter_get_parent_name(inst);
	snap.source_name = name ? name : "(unknown)";
	snap.status = av_sync_filter_get_status(inst);
	snap.enabled = av_sync_filter_get_sync_enabled(inst);
	snap.offset_ms = av_sync_filter_get_smoothed_offset_ms(inst);
	snap.confidence = av_sync_filter_get_last_confidence(inst);
	const char *ref = av_sync_filter_get_reference_name(inst);
	snap.reference = ref ? ref : "(none)";
	vec->push_back(std::move(snap));
}

class AVSyncDock : public QDockWidget {
	Q_OBJECT
public:
	explicit AVSyncDock(QWidget *parent = nullptr);
	~AVSyncDock();

private slots:
	void refresh();

private:
	QTableWidget *table_;
	QTimer *timer_;
	void populateTable(const std::vector<filter_snapshot> &rows);
};

AVSyncDock::AVSyncDock(QWidget *parent) : QDockWidget(parent)
{
	setWindowTitle(obs_module_text("AVSync.DockTitle"));
	setObjectName(QStringLiteral("obs-av-sync-dock"));

	table_ = new QTableWidget(0, 5, this);
	table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table_->setFocusPolicy(Qt::NoFocus);
	table_->setSelectionBehavior(QAbstractItemView::SelectRows);
	table_->setSelectionMode(QAbstractItemView::SingleSelection);

	QStringList headers;
	headers << obs_module_text("AVSync.Table.SourceName")
	        << obs_module_text("AVSync.Table.Status")
	        << obs_module_text("AVSync.Table.Offset")
	        << obs_module_text("AVSync.Table.Confidence")
	        << obs_module_text("AVSync.Table.Reference");
	table_->setHorizontalHeaderLabels(headers);
	table_->horizontalHeader()->setStretchLastSection(true);
	table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

	int minHeight = table_->horizontalHeader()->height()
	                + 4 * table_->verticalHeader()->defaultSectionSize();
	table_->setMinimumHeight(minHeight);

	setWidget(table_);

	timer_ = new QTimer(this);
	connect(timer_, &QTimer::timeout, this, &AVSyncDock::refresh);
	timer_->start(500); /* 2 Hz per D-11 */
}

AVSyncDock::~AVSyncDock()
{
	if (timer_) {
		timer_->stop();
	}
}

void AVSyncDock::refresh()
{
	std::vector<filter_snapshot> rows;
	av_sync_filter_enum_instances(enum_callback, &rows);

	std::sort(rows.begin(), rows.end(),
	          [](const filter_snapshot &a, const filter_snapshot &b) {
		          return a.source_name < b.source_name;
	          });

	populateTable(rows);
}

void AVSyncDock::populateTable(const std::vector<filter_snapshot> &rows)
{
	if (rows.empty()) {
		table_->setRowCount(1);
		QTableWidgetItem *msg = new QTableWidgetItem(
			obs_module_text("AVSync.Dock.NoCameras"));
		msg->setFlags(msg->flags() & ~Qt::ItemIsEditable);
		table_->setItem(0, 0, msg);
		for (int c = 1; c < 5; ++c) {
			QTableWidgetItem *empty = new QTableWidgetItem("");
			empty->setFlags(empty->flags() & ~Qt::ItemIsEditable);
			table_->setItem(0, c, empty);
		}
		return;
	}

	table_->setRowCount(static_cast<int>(rows.size()));

	for (size_t i = 0; i < rows.size(); ++i) {
		const filter_snapshot &r = rows[i];
		int row = static_cast<int>(i);

		/* Column 0 — Source Name */
		QTableWidgetItem *nameItem = new QTableWidgetItem(
			QString::fromUtf8(r.source_name.c_str()));
		nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
		table_->setItem(row, 0, nameItem);

		/* Column 1 — Status (text + color) */
		QString statusText;
		QColor statusColor;
		if (!r.enabled) {
			statusText = obs_module_text("AVSync.Status.Disabled");
			statusColor = QColor(158, 158, 158); /* gray */
		} else if (r.status == 0) {
			statusText = obs_module_text("AVSync.Status.Measuring");
			statusColor = QColor(255, 193, 7); /* amber */
		} else if (r.status == 1) {
			statusText = obs_module_text("AVSync.Status.Synced");
			statusColor = QColor(76, 175, 80); /* green */
		} else {
			statusText = obs_module_text("AVSync.Status.OutOfRange");
			statusColor = QColor(244, 67, 54); /* red */
		}

		QTableWidgetItem *statusItem = new QTableWidgetItem(statusText);
		statusItem->setForeground(QBrush(statusColor));
		statusItem->setFlags(statusItem->flags() & ~Qt::ItemIsEditable);
		table_->setItem(row, 1, statusItem);

		/* Column 2 — Offset (ms) */
		QTableWidgetItem *offsetItem = new QTableWidgetItem(
			QString::number(r.offset_ms, 'f', 1));
		offsetItem->setFlags(offsetItem->flags() & ~Qt::ItemIsEditable);
		table_->setItem(row, 2, offsetItem);

		/* Column 3 — Confidence */
		QTableWidgetItem *confItem = new QTableWidgetItem(
			QString::number(r.confidence, 'f', 2));
		confItem->setFlags(confItem->flags() & ~Qt::ItemIsEditable);
		table_->setItem(row, 3, confItem);

		/* Column 4 — Reference */
		QTableWidgetItem *refItem = new QTableWidgetItem(
			QString::fromUtf8(r.reference.c_str()));
		refItem->setFlags(refItem->flags() & ~Qt::ItemIsEditable);
		table_->setItem(row, 4, refItem);
	}
}

static AVSyncDock *g_dock = nullptr;

extern "C" bool av_sync_dock_create(void)
{
	if (g_dock)
		return true;

	g_dock = new AVSyncDock();
	if (!g_dock)
		return false;

	obs_frontend_add_dock_by_id("obs-av-sync-dock",
	    obs_module_text("AVSync.DockTitle"), g_dock);
	return true;
}

extern "C" void av_sync_dock_destroy(void)
{
	if (g_dock) {
		obs_frontend_remove_dock("obs-av-sync-dock");
		delete g_dock;
		g_dock = nullptr;
	}
}

#include "av_sync_dock.moc"
