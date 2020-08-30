/*
*  Copyright 2019  Michail Vourlakos <mvourlakos@gmail.com>
*
*  This file is part of Latte-Dock
*
*  Latte-Dock is free software; you can redistribute it and/or
*  modify it under the terms of the GNU General Public License as
*  published by the Free Software Foundation; either version 2 of
*  the License, or (at your option) any later version.
*
*  Latte-Dock is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "synchronizer.h"

//! local
#include "importer.h"
#include "manager.h"
#include "../apptypes.h"
#include "../data/layoutdata.h"
#include "../lattecorona.h"
#include "../layout/centrallayout.h"
#include "../layout/genericlayout.h"
#include "../settings/universalsettings.h"
#include "../templates/templatesmanager.h"
#include "../view/view.h"

// Qt
#include <QDir>
#include <QFile>

// Plasma
#include <Plasma/Containment>

// KDE
#include <KActivities/Consumer>
#include <KActivities/Controller>
#include <KWindowSystem>

#define LAYOUTSINITINTERVAL 350

namespace Latte {
namespace Layouts {

Synchronizer::Synchronizer(QObject *parent)
    : QObject(parent),
      m_activitiesController(new KActivities::Controller)
{
    m_manager = qobject_cast<Manager *>(parent);

    connect(this, &Synchronizer::layoutsChanged, this, &Synchronizer::reloadAssignedLayouts);

    //! KWin update Disabled Borders
    connect(this, &Synchronizer::centralLayoutsChanged, this, &Synchronizer::updateKWinDisabledBorders);
    connect(m_manager->corona()->universalSettings(), &UniversalSettings::canDisableBordersChanged, this, &Synchronizer::updateKWinDisabledBorders);


    //! KActivities tracking
    connect(m_manager->corona()->activitiesConsumer(), &KActivities::Consumer::currentActivityChanged,
            this, &Synchronizer::onCurrentActivityChanged);

    connect(m_manager->corona()->activitiesConsumer(), &KActivities::Consumer::runningActivitiesChanged,
            this, [&]() {
        if (m_manager->memoryUsage() == MemoryUsage::MultipleLayouts) {
            syncMultipleLayoutsToActivities();
        }
    });
}

Synchronizer::~Synchronizer()
{
    m_activitiesController->deleteLater();
}

KActivities::Controller *Synchronizer::activitiesController() const
{
    return m_activitiesController;
}

bool Synchronizer::latteViewExists(Latte::View *view) const
{
    for (const auto layout : m_centralLayouts) {
        for (const auto &v : layout->latteViews()) {
            if (v == view) {
                return true;
            }
        }
    }

    return false;
}

bool Synchronizer::layoutExists(QString layoutName) const
{
    return m_layouts.containsName(layoutName);
}


bool Synchronizer::isAssigned(QString layoutName) const
{
    for(auto activityid : m_assignedLayouts.keys()) {
        if (m_assignedLayouts[activityid].contains(layoutName)) {
            return true;
        }
    }

    return false;
}

int Synchronizer::centralLayoutPos(QString id) const
{
    for (int i = 0; i < m_centralLayouts.size(); ++i) {
        CentralLayout *layout = m_centralLayouts.at(i);

        if (layout->name() == id) {
            return i;
        }
    }

    return -1;
}

QString Synchronizer::layoutPath(QString layoutName)
{
    QString path = Layouts::Importer::layoutUserFilePath(layoutName);

    if (!QFile(path).exists()) {
        path = "";
    }

    return path;
}

QStringList Synchronizer::activities()
{
    return m_manager->corona()->activitiesConsumer()->activities();
}

QStringList Synchronizer::freeActivities()
{
    QStringList frees = activities();

    for(auto assigned : m_assignedLayouts.keys()) {
        frees.removeAll(assigned);
    }

    return frees;
}

QStringList Synchronizer::runningActivities()
{
    return m_manager->corona()->activitiesConsumer()->runningActivities();
}

QStringList Synchronizer::freeRunningActivities()
{
    QStringList fActivities;

    for (const auto &activity : runningActivities()) {
        if (!m_assignedLayouts.contains(activity)) {
            fActivities.append(activity);
        }
    }

    return fActivities;
}

QStringList Synchronizer::centralLayoutsNames()
{
    QStringList names;

    if (m_manager->memoryUsage() == MemoryUsage::SingleLayout) {
        names << m_centralLayouts.at(0)->name();
    } else {
        for (int i = 0; i < m_centralLayouts.size(); ++i) {
            CentralLayout *layout = m_centralLayouts.at(i);
            names << layout->name();
        }
    }

    return names;
}

QStringList Synchronizer::currentLayoutsNames() const
{
    QList<CentralLayout *> currents = currentLayouts();
    QStringList currentNames;

    for (int i = 0; i < currents.size(); ++i) {
        CentralLayout *layout = currents.at(i);
        currentNames << layout->name();
    }

    return currentNames;
}

QStringList Synchronizer::layouts() const
{
    return m_layouts.names();
}

QStringList Synchronizer::menuLayouts() const
{
    QStringList menulayouts;

    for (int i=0; i<m_layouts.rowCount(); ++i) {
        if (!m_layouts[i].isShownInMenu) {
            continue;
        }

        if (m_manager->memoryUsage() == MemoryUsage::SingleLayout
                || (m_manager->memoryUsage() == MemoryUsage::MultipleLayouts && !m_layouts[i].activities.isEmpty())) {
            menulayouts << m_layouts[i].name;
        }
    }

    for (const auto layout : m_centralLayouts) {
        if (!menulayouts.contains(layout->name())) {
            menulayouts.prepend(layout->name());
        }
    }

    return menulayouts;
}

Data::LayoutsTable Synchronizer::layoutsTable() const
{
    return m_layouts;
}

void Synchronizer::setLayoutsTable(const Data::LayoutsTable &table)
{
    if (m_layouts == table) {
        return;
    }

    m_layouts = table;
    emit layoutsChanged();
}

void Synchronizer::updateLayoutsTable()
{
    for (int i = 0; i < m_centralLayouts.size(); ++i) {
        CentralLayout *layout = m_centralLayouts.at(i);

        if (m_layouts.containsId(layout->file())) {
            m_layouts[layout->file()] = layout->data();
        }
    }

    for (int i = 0; i < m_layouts.rowCount(); ++i) {
        if (m_layouts[i].isBroken && !m_layouts[i].isActive) {
            CentralLayout central(this, m_layouts[i].id);
            m_layouts[i].isBroken = central.isBroken();
        }
    }
}

CentralLayout *Synchronizer::centralLayout(QString layoutname) const
{
    for (int i = 0; i < m_centralLayouts.size(); ++i) {
        CentralLayout *layout = m_centralLayouts.at(i);

        if (layout->name() == layoutname) {
            return layout;
        }
    }

    return nullptr;
}

QList<CentralLayout *> Synchronizer::currentLayouts() const
{
    QList<CentralLayout *> layouts;

    if (m_manager->memoryUsage() == MemoryUsage::SingleLayout) {
        layouts << m_centralLayouts.at(0);
    } else {
        for (auto layout : m_centralLayouts) {
            if (layout->isOnAllActivities() || layout->appliedActivities().contains(m_manager->corona()->activitiesConsumer()->currentActivity())) {
                layouts << layout;
            }
        }
    }

    return layouts;
}

QList<CentralLayout *> Synchronizer::centralLayoutsForActivity(const QString activityid) const
{
    QList<CentralLayout *> layouts;

    if (m_manager->memoryUsage() == MemoryUsage::SingleLayout) {
        layouts << m_centralLayouts.at(0);
    } else {
        for (auto layout : m_centralLayouts) {
            if (layout->isOnAllActivities() || layout->appliedActivities().contains(activityid)) {
                layouts << layout;
            }
        }
    }

    return layouts;
}

QList<Latte::View *> Synchronizer::currentViews() const
{
    QList<Latte::View *> views;

    for(auto layout : currentLayouts()) {
        views << layout->latteViews();
    }

    return views;
}

QList<Latte::View *> Synchronizer::currentViewsWithPlasmaShortcuts() const
{
    QList<Latte::View *> views;

    for(auto layout : currentLayouts()) {
        views << layout->viewsWithPlasmaShortcuts();
    }

    return views;
}

QList<Latte::View *> Synchronizer::sortedCurrentViews() const
{
    QList<Latte::View *> views = currentViews();

    return Layout::GenericLayout::sortedLatteViews(views);
}

QList<Latte::View *> Synchronizer::viewsBasedOnActivityId(const QString &id) const
{
    QList<Latte::View *> views;

    for(auto layout : centralLayoutsForActivity(id)) {
        views << layout->latteViews();
    }

    return views;
}

Layout::GenericLayout *Synchronizer::layout(QString layoutname) const
{
    Layout::GenericLayout *l = centralLayout(layoutname);

    return l;
}

Latte::View *Synchronizer::viewForContainment(uint id)
{
    for (auto layout : m_centralLayouts) {
        Latte::View *view = layout->viewForContainment(id);

        if (view) {
            return view;
        }
    }

    return nullptr;
}

Latte::View *Synchronizer::viewForContainment(Plasma::Containment *containment)
{
    for (auto layout : m_centralLayouts) {
        Latte::View *view = layout->viewForContainment(containment);

        if (view) {
            return view;
        }
    }

    return nullptr;
}

void Synchronizer::addLayout(CentralLayout *layout)
{
    if (!m_centralLayouts.contains(layout)) {
        m_centralLayouts.append(layout);
        layout->initToCorona(m_manager->corona());
    }
}

void Synchronizer::onCurrentActivityChanged(const QString &id)
{
    if (m_manager->memoryUsage() == MemoryUsage::MultipleLayouts) {
        updateKWinDisabledBorders();
    }
}

void Synchronizer::hideAllViews()
{
    for (const auto layout : m_centralLayouts) {
        emit currentLayoutIsSwitching(layout->name());
    }
}

void Synchronizer::pauseLayout(QString layoutName)
{
    if (m_manager->memoryUsage() == MemoryUsage::MultipleLayouts) {
        CentralLayout *layout = centralLayout(layoutName);

        if (layout && !layout->activities().isEmpty()) {
            int i = 0;

            for (const auto &activityId : layout->activities()) {
                //! Stopping the activities must be done asynchronous because otherwise
                //! the activity manager cant close multiple activities
                QTimer::singleShot(i * 1000, [this, activityId]() {
                    m_activitiesController->stopActivity(activityId);
                });

                i = i + 1;
            }
        }
    }
}

void Synchronizer::syncActiveLayoutsToOriginalFiles()
{
    if (m_manager->memoryUsage() == MemoryUsage::MultipleLayouts) {
        for (const auto layout : m_centralLayouts) {
            layout->syncToLayoutFile();
        }
    }
}

void Synchronizer::syncLatteViewsToScreens()
{
    for (const auto layout : m_centralLayouts) {
        layout->syncLatteViewsToScreens();
    }
}

void Synchronizer::unloadCentralLayout(CentralLayout *layout)
{
    int pos = m_centralLayouts.indexOf(layout);

    if (pos>=0) {
        CentralLayout *central = m_centralLayouts.takeAt(0);

        if (m_multipleModeInitialized) {
            central->syncToLayoutFile(true);
        }

        central->unloadLatteViews();
        central->unloadContainments();

        if (m_multipleModeInitialized) {
            m_manager->clearUnloadedContainmentsFromLinkedFile(central->unloadedContainmentsIds(), true);
        }

        delete central;
    }
}

void Synchronizer::initLayouts()
{
    m_layouts.clear();

    QDir layoutDir(Layouts::Importer::layoutUserDir());
    QStringList filter;
    filter.append(QString("*.layout.latte"));
    QStringList files = layoutDir.entryList(filter, QDir::Files | QDir::NoSymLinks);

    for (const auto &layout : files) {
        if (layout.contains(Layout::MULTIPLELAYOUTSHIDDENNAME)) {
            //! IMPORTANT: DON'T ADD MultipleLayouts hidden file in layouts list
            continue;
        }

        QString layoutpath = layoutDir.absolutePath() + "/" + layout;
        onLayoutAdded(layoutpath);
    }

    emit layoutsChanged();

    if (!m_isLoaded) {
        m_isLoaded = true;
        connect(m_manager->corona()->templatesManager(), &Latte::Templates::Manager::newLayoutAdded, this, &Synchronizer::onLayoutAdded);
        connect(m_manager->importer(), &Latte::Layouts::Importer::newLayoutAdded, this, &Synchronizer::onLayoutAdded);
    }
}

void Synchronizer::onLayoutAdded(const QString &layout)
{
    CentralLayout centralLayout(this, layout);
    m_layouts.insertBasedOnName(centralLayout.data());

    if (m_isLoaded) {
        emit layoutsChanged();
    }
}

void Synchronizer::reloadAssignedLayouts()
{
    m_assignedLayouts.clear();

    for (int i=0; i< m_layouts.rowCount(); ++i) {
        for (const auto &activity : m_layouts[i].activities) {
            if (m_assignedLayouts.contains(activity)) {
                m_assignedLayouts[activity] << m_layouts[i].name;
            } else {
                m_assignedLayouts[activity] = QStringList(m_layouts[i].name);
            }
        }
    }
}

void Synchronizer::unloadLayouts()
{
    //! Unload all CentralLayouts
    while (!m_centralLayouts.isEmpty()) {
        CentralLayout *layout = m_centralLayouts.at(0);
        unloadCentralLayout(layout);
    }

    m_multipleModeInitialized = false;
}

bool Synchronizer::memoryInitialized() const
{
    return ((m_manager->memoryUsage() == MemoryUsage::SingleLayout && m_centralLayouts.size()>0)
            || (m_manager->memoryUsage() == MemoryUsage::MultipleLayouts && m_multipleModeInitialized));
}

bool Synchronizer::initSingleMode(QString layoutName)
{
    QString layoutpath = layoutName.isEmpty() ? layoutPath(m_manager->corona()->universalSettings()->singleModeLayoutName()) : layoutPath(layoutName);

    if (layoutpath.isEmpty()) {
        qDebug() << "Layout : " << layoutName << " was not found...";
        return false;
    }

    if (m_centralLayouts.size() > 0) {
        emit currentLayoutIsSwitching(m_centralLayouts[0]->name());
    }

    //! this code must be called asynchronously because it can create crashes otherwise.
    //! Tasks plasmoid case that triggers layouts switching through its context menu
    QTimer::singleShot(LAYOUTSINITINTERVAL, [this, layoutName, layoutpath]() {
        qDebug() << " ... initializing layout in single mode : " << layoutName << " - " << layoutpath;
        unloadLayouts();

        //! load the main layout/corona file
        CentralLayout *newLayout = new CentralLayout(this, layoutpath, layoutName);
        addLayout(newLayout);

        m_manager->loadLatteLayout(layoutpath);

        emit centralLayoutsChanged();

        m_manager->corona()->universalSettings()->setSingleModeLayoutName(layoutName);
    });

    return true;
}

bool Synchronizer::initMultipleMode(QString layoutName)
{
    if (m_multipleModeInitialized) {
        return false;
    }

    for (const auto layout : m_centralLayouts) {
        emit currentLayoutIsSwitching(layout->name());
    }

    //! this code must be called asynchronously because it can create crashes otherwise.
    //! Tasks plasmoid case that triggers layouts switching through its context menu
    QTimer::singleShot(LAYOUTSINITINTERVAL, [this, layoutName]() {
        qDebug() << " ... initializing layout in multiple mode : " << layoutName ;
        unloadLayouts();

        m_manager->loadLatteLayout(layoutPath(QString(Layout::MULTIPLELAYOUTSHIDDENNAME)));

        m_multipleModeInitialized = true;

        emit centralLayoutsChanged();

        syncMultipleLayoutsToActivities();
    });

    return true;
}

bool Synchronizer::switchToLayoutInSingleMode(QString layoutName)
{
    if (!memoryInitialized() || m_manager->memoryUsage() != MemoryUsage::SingleLayout) {
        return false;
    }

    if (m_centralLayouts.size()>0 && m_centralLayouts[0]->name() == layoutName) {
        return true;
    }

    return initSingleMode(layoutName);
}

bool Synchronizer::switchToLayoutInMultipleMode(QString layoutName)
{
    if (!memoryInitialized() || m_manager->memoryUsage() != MemoryUsage::MultipleLayouts) {
        return false;
    }

    CentralLayout *layout = centralLayout(layoutName);

    if (layout) {
        QStringList appliedActivities = layout->appliedActivities();
        QString nextActivity = !layout->lastUsedActivity().isEmpty() ? layout->lastUsedActivity() : appliedActivities[0];

        if (!appliedActivities.contains(m_manager->corona()->activitiesConsumer()->currentActivity())) {
            //! it means we are at a foreign activity and we can switch to correct one
            m_activitiesController->setCurrentActivity(nextActivity);
            return true;
        }
    } else {
        syncMultipleLayoutsToActivities();
    }

    return true;
}


bool Synchronizer::switchToLayout(QString layoutName, MemoryUsage::LayoutsMemory newMemoryUsage)
{
    qDebug() << " >>>>> SWITCHING >> " << layoutName << " __ from memory: " << m_manager->memoryUsage() << " to memory: " << newMemoryUsage;

    if (!memoryInitialized() || newMemoryUsage != m_manager->memoryUsage()) {
        //! Initiate Layouts memory properly
        if (newMemoryUsage != MemoryUsage::Current) {
            m_manager->setMemoryUsage(newMemoryUsage);
        } else {
            newMemoryUsage = m_manager->memoryUsage();
        }

        return (newMemoryUsage == MemoryUsage::SingleLayout ? initSingleMode(layoutName) : initMultipleMode(layoutName));
    }

    if (m_manager->memoryUsage() == MemoryUsage::SingleLayout) {
        return switchToLayoutInSingleMode(layoutName);
    } else {
        return switchToLayoutInMultipleMode(layoutName);
    }
}

void Synchronizer::syncMultipleLayoutsToActivities()
{
    qDebug() << "   ----  --------- ------    syncMultipleLayoutsToActivities       -------   ";
    qDebug() << "   ----  --------- ------    -------------------------------       -------   ";

    QStringList layoutNamesToUnload;
    QStringList layoutNamesToLoad;
    QStringList currentNames = centralLayoutsNames();

    //! discover OnAllActivities layouts
    if (m_assignedLayouts.contains(Data::Layout::ALLACTIVITIESID)) {
        layoutNamesToLoad << m_assignedLayouts[Data::Layout::ALLACTIVITIESID];
    }

    //! discover ForFreeActivities layouts
    if (m_assignedLayouts.contains(Data::Layout::FREEACTIVITIESID)) {
        layoutNamesToLoad << m_assignedLayouts[Data::Layout::FREEACTIVITIESID];
    }

    //! discover layouts assigned to explicit activities based on running activities
    for (const auto &activity : runningActivities()) {
        if (KWindowSystem::isPlatformWayland() && (m_activitiesController->currentActivity() != activity)){
            //! Wayland Protection: Plasma wayland does not support yet Activities for windows
            //! but we can load the layouts that belong OnAllActivities + (ForFreeActivities OR SpecificActivity)
            continue;
        }

        if (m_assignedLayouts.contains(activity)) {
            layoutNamesToLoad << m_assignedLayouts[activity];
        }
    }

    //! discover layouts that must be unloaded because of running activities changes
    for (const auto layout : m_centralLayouts) {
        if (!layoutNamesToLoad.contains(layout->name())) {
            layoutNamesToUnload << layout->name();
        }
    }

    QString defaultForcedLayout;

    //! Safety
    if (layoutNamesToLoad.isEmpty()) {
        //! If no layout is found then force loading Default Layout
        QString layoutPath = m_manager->corona()->templatesManager()->newLayout("", i18n(Templates::DEFAULTLAYOUTTEMPLATENAME));
        layoutNamesToLoad << Layout::AbstractLayout::layoutName(layoutPath);
        m_manager->setOnAllActivities(layoutNamesToLoad[0]);
        defaultForcedLayout = layoutNamesToLoad[0];
    }

    QStringList newlyActivatedLayouts;

    //! Add needed Layouts based on Activities settings
    for (const auto &layoutname : layoutNamesToLoad) {
        if (!centralLayout(layoutname)) {
            CentralLayout *newLayout = new CentralLayout(this, QString(layoutPath(layoutname)), layoutname);

            if (newLayout) {
                qDebug() << "ACTIVATING LAYOUT ::::: " << layoutname;
                addLayout(newLayout);
                newLayout->importToCorona();

                if (!defaultForcedLayout.isEmpty() && defaultForcedLayout == layoutname) {
                    emit newLayoutAdded(newLayout->data());
                }

                newlyActivatedLayouts << newLayout->name();
            }
        }
    }

    if (m_manager->corona()->universalSettings()->showInfoWindow()) {
        if (newlyActivatedLayouts.count() == 1) {
            m_manager->showInfoWindow(i18n("Activating layout: <b>%0</b> ...").arg(newlyActivatedLayouts[0]), 4000, QStringList(Data::Layout::ALLACTIVITIESID));
        } else if (newlyActivatedLayouts.count() > 1) {
            m_manager->showInfoWindow(i18n("Activating layouts: <b>%0</b> ...").arg(newlyActivatedLayouts.join(", ")), 4000, QStringList(Data::Layout::ALLACTIVITIESID));
        }
    }

    //! Unload no needed Layouts

    //! hide layouts that will be removed in the end
    if (!layoutNamesToUnload.isEmpty()) {
        for (const auto layoutname : layoutNamesToUnload) {
            emit currentLayoutIsSwitching(layoutname);
        }

        QTimer::singleShot(LAYOUTSINITINTERVAL, [this, layoutNamesToUnload]() {
            unloadLayouts(layoutNamesToUnload);
        });
    }

    qSort(currentNames);
    qSort(layoutNamesToLoad);

    if (currentNames != layoutNamesToLoad) {
        emit centralLayoutsChanged();
    }
}

void Synchronizer::unloadLayouts(const QStringList &layoutNames)
{
    if (layoutNames.isEmpty()) {
        return;
    }

    //! Unload no needed Layouts
    for (const auto &layoutname : layoutNames) {
        CentralLayout *layout = centralLayout(layoutname);
        int posLayout = centralLayoutPos(layoutname);

        if (posLayout >= 0) {
            qDebug() << "REMOVING LAYOUT ::::: " << layoutname;
            m_centralLayouts.removeAt(posLayout);

            layout->syncToLayoutFile(true);
            layout->unloadContainments();
            layout->unloadLatteViews();
            m_manager->clearUnloadedContainmentsFromLinkedFile(layout->unloadedContainmentsIds());
            delete layout;
        }
    }

    emit centralLayoutsChanged();
}

void Synchronizer::updateKWinDisabledBorders()
{
    if (!m_manager->corona()->universalSettings()->canDisableBorders()) {
        m_manager->corona()->universalSettings()->kwin_setDisabledMaximizedBorders(false);
    } else {
        if (m_manager->corona()->layoutsManager()->memoryUsage() == MemoryUsage::SingleLayout) {
            m_manager->corona()->universalSettings()->kwin_setDisabledMaximizedBorders(m_centralLayouts.at(0)->disableBordersForMaximizedWindows());
        } else if (m_manager->corona()->layoutsManager()->memoryUsage() == MemoryUsage::MultipleLayouts) {
            QList<CentralLayout *> centrals = centralLayoutsForActivity(m_manager->corona()->activitiesConsumer()->currentActivity());

            for (int i = 0; i < centrals.size(); ++i) {
                CentralLayout *layout = centrals.at(i);

                if (layout->disableBordersForMaximizedWindows()) {
                    m_manager->corona()->universalSettings()->kwin_setDisabledMaximizedBorders(true);
                    return;
                }
            }

            m_manager->corona()->universalSettings()->kwin_setDisabledMaximizedBorders(false);
        }
    }
}

}
} // end of namespace
