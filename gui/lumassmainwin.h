 /******************************************************************************
 * Created by Alexander Herzig
 * Copyright 2010-2016 Landcare Research New Zealand Ltd
 *
 * This file is part of 'LUMASS', which is free software: you can redistribute
 * it and/or modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 ******************************************************************************/
#ifndef LUMASSMainWin_H
#define LUMASSMainWin_H

#define ctxLUMASSMainWin "LUMASSMainWin"

#include <QMdiArea>
#include <QMainWindow>
#include <QMouseEvent>
#include <QActionEvent>
#include <QChildEvent>
#include <QMetaType>
#include <QMap>
#include <QLabel>
#include <QSharedPointer>
#include <QStandardItemModel>
#include <QProgressBar>
#include <QToolBox>
#include <QListWidget>
#include "qttreepropertybrowser.h"

#include <QtWebSockets/QWebSocketServer>
#include <QSslError>

//#include <QTcpServer>
//#include <QTcpSocket>
//#include <QNetworkSession>

//#include <qsslerror.h>


// OGR
#include "ogrsf_frmts.h"

//#include "otbGDALRATImageFileReader.h"
//#include "otbImage.h"
#include "vtkObject.h"
#include "vtkCommand.h"
#include "vtkActor.h"
#include "vtkPolyData.h"
#include "vtkRenderWindowInteractor.h"
#include "vtkOrientationMarkerWidget.h"
#include "vtkSmartPointer.h"
#include "vtkCellPicker.h"
#include "NMVtkInteractorStyleImage.h"

#include "LUMASSConfig.h"
//#include "NMLayer.h"

#include "NMSqlTableView.h"
#include "NMLogger.h"

#include "otbSQLiteTable.h"
#include "mpParser.h"


// rasdaman
#ifdef BUILD_RASSUPPORT
  #include "RasdamanConnector.hh"
  #include "otbRasdamanImageReader.h"
  #include "otbRasdamanImageIO.h"
  #include "otbRasdamanImageIOFactory.h"
#endif

#include "NMMosra.h"

//QT_FORWARD_DECLARE_CLASS(QWebSocketServer)
//QT_FORWARD_DECLARE_CLASS(QWebSocket)

class NMModelController;
class NMModelComponent;
class NMComponentEditor;
class ModelComponentList;
class vtkRenderer;
class vtkEventQtSlotConnect;
class vtkObject;
class vtkCommand;
class vtkTable;
class NMLayer;
class NMTableView;
class NMVectorLayer;
class NMLogWidget;
class QtProperty;
class NMAbstractAction;
class NMListWidget;

namespace Ui
{
    class LUMASSMainWin;
}

class LUMASSMainWin : public QMainWindow
{
    Q_OBJECT

public:
    LUMASSMainWin(QWidget *parent = 0);
    ~LUMASSMainWin();

    friend class NMGlobalHelper;

    enum TableViewType
        {
            NM_TABVIEW_SCENE,
            NM_TABVIEW_STANDALONE
        };

    vtkRenderWindow* getRenderWindow(void);
    const vtkRenderer* getBkgRenderer(void);
    const vtkRenderer* getScaleRenderer(void);
    const QWebSocketServer* getSocketServer(void) {return mServer;}
    const QList<QWebSocket*> getSocketClients(void) {return mClientList;}
    void displayChart(vtkTable* srcTab);
    void updateCoordLabel(const QString& newCoords);
    const NMComponentEditor* getCompEditor(void);

    NMSqlTableView *openCreateTable(TableViewType tvType=NM_TABVIEW_STANDALONE,
                                    bool bOpen=true);

    NMSqlTableView* importTable(const QString& fileName, TableViewType tvType,
                                bool bOpen, const QString& tableName="");

    QString selectSqliteTable(const QString& dbFileName);

    NMSqlTableView* createTableView(otb::SQLiteTable::Pointer sqlTab);
    NMSqlTableView* createTableView(const QString& dbFileName, const QString& tableName, const QString& viewName);

    QMap<QString, QPair<otb::SQLiteTable::Pointer, QSharedPointer<NMSqlTableView> > >&
        getTableList(void) {return mTableList;}
    QMultiMap<QString, QString> getTableDbNamesList(void) {return mTableDbNames; }
    ModelComponentList* getLayerList(){return this->mLayerList;}

    /* opens a db connection to the given database and
     * returns the associated connection name; if a connection
     * has already been established with the requested bReadWrite setting,
     * the existing connection name returned; if the requested bReadWrite setting
     * does not align with the request, the db is closed and re-opened with the
     * requested setting and the associated connection name returned.
     * Whenever an empty connection name is return, something went wrong!
     */
    QString getDbConnection(const QString& dbFileName, bool bReadWrite=true, const QString& conSuffix=QString());
    QString getSessionDbFileName(void) {return mSessionDbFileName;}

    /*! removes a database connection by providing either the database's
     *  filename or its connection name
     *
     *  @param dbFileName  database filename
     *  @param conname     connection name
     *  @returns true      if the database connection was found and removed

        */
    bool removeDbConnection(const QString& dbFileName, const QString& conname);



    void checkRemoveLayerInfo(NMLayer* l);
    void checkInteractiveLayer();

    NMLogWidget* getLogWidget(void);
    NMLogger* getLogger(){return mLogger;}

    QStringList getUserToolsList(void);
    const NMAbstractAction* getUserTool(const QString& toolName);

#ifdef BUILD_RASSUPPORT
    RasdamanConnector* getRasdamanConnector(void);
#endif

signals:
    void signalIsIn3DMode(bool in3d);
    void noExclusiveToolSelected(void);
    void isAboutToClose(void);
    //void componentOfInterest(const QString&);
    void windowLoaded(void);
    void settingsUpdated(const QString&, QVariant);

public slots:

#ifdef BUILD_RASSUPPORT
    void loadRasdamanLayer();
    void updateRasMetaView();
    void fetchRasLayer(const QString& imagespec,
            const QString& covname);
    void eraseRasLayer(const QString& imagespec);
#endif
    void connectImageLayerProcSignals(NMLayer* layer);
    void showBusyStart();
    void showBusyValue(int);
    void showBusyEnd();
    void loadImageLayer();
    void loadImageLayer(const QString& fileName);
    void import3DPointSet();			// imports char (" " | "," | ";" | "\t") seperated text (x,y,z)
    void toggle3DStereoMode();
    void toggle3DSimpleMode();
    void loadVTKPolyData();			// loads VTK *vtp PolyData
    void loadVectorLayer();
    void doMOSO();
    void showComponentsView(bool);
    void showComponentsInfoView(bool);
    void showMapView(bool);
    void showModelView(bool);
    void showNotificationsView(bool);
    void showTable();
    void showScaleBar(bool);
    void showCoordinateAxes(bool);
    void mapViewMode();
    void modelViewMode();
    void updateCoords(vtkObject* obj);
    void removeAllObjects();
    void pickObject(vtkObject* obj);
    void zoomFullExtent();
    void saveAsVtkPolyData();
    void saveSelectionAsVtkPolyData();
    void test();
    void saveAsVectorLayerOGR();
    void saveImageFile();
    void saveMapAsImage();
    void makeZSliceMovie();
    void updateLayerInfo(NMLayer* l, long long cellId);
    void importTableObject();
    void aboutLUMASS();
    void addLayerToCompList();
    void addLayerToCompList(NMLayer* layer);
    void image2PolyData(vtkImageData* img, QList<int>& unitIds);
    void setMapBackgroundColour();
    void tableObjectVisibility(QListWidgetItem* item);
    void removeTableObject(QListWidgetItem* item, QPoint globalPos);
    void tableObjectViewClosed();
    void treeSelTopDown(void);
    void treeSelBottomUp(void);
    void treeFindLoops(void);
    void deleteTableObject(const QString &);
    void convertImageToPolyData();
    void swapWindowLayout(QAction *act);
    void modelViewActivated(QObject *);
    void clearSelection(void);
    bool isInDarkMode(void);
    void setDarkMode(bool bdark);

    void infoTableHeaderClicked(int idx);
    void infoTableHeaderContextMenu();

    void pan(bool toggled);
    void zoomIn(bool toggled);
    void zoomOut(bool toggled);
    void toggleSelectTool(bool toggled);
    void toggleLinkTool(bool toggled);
    void zoomToContent();
    void zoomToCoords(double* box);
    void updateExclusiveActions(const QString& checkedAction,
                                bool toggled);
    void forwardModelConfigChanged(void);

    /*! SLICE SELECTION */
    void setImageLayerZSliceIdx(int delta);


    /*! LUMASS settings & session configuration */
    void createNewSessionDb();
    void openSessionDb(const QString& sessionDb);

    void configureSettings(void);
    //void searchModelComponent(void);

    void selectUserTool(bool toggled);

    // SYSTEM SLOTS & SETTINGS
    void show();
    void readSettings(void);
    QMenu* createPopupMenu(void);

    // PROCESS COMPONENTS & USER MODELS & TOOLS
    void addUserToolBar();
    void createUserToolBar(const QString& tbname,
                           const QByteArray& ba=QByteArray());
    void updateLastToolBar(void);
    void removeUserToolBar(void);
    void addUserModelToolToToolBar();

    QString getUserModelPath(const QString& model);
    void executeUserModel(void);
    void updateUserModelTriggerParameters(NMAbstractAction* uact);
    void scanUserModels(void);
    void displayUserModelOutput(void);
    void loadUserModelTool(const QString& modelPath, const QString& userModel, const QString& toolBarName);
    void removeUserTool(NMAbstractAction* act);


    // logging

    void appendLogMsg(const QString& msg);
    void appendHtmlMsg(const QString& msg);

    QStringList getNextParamExpr(const QString& expr);

    QStandardItemModel* prepareResChartModel(vtkTable* restab);

    virtual bool notify(QObject* receiver, QEvent* event);

    static int sqlite_resCallback(void *NotUsed, int argc, char **argv, char **azColName);

//    inline bool checkTree(const int& rootId, const int& idIdx,
//                   const int& dnIdx, QList<int>& idHistory,
//                   QAbstractItemModel* tableModel, const int &nrows);


    /// ====================================================================
    ///                 SOME GISy HYDROy FUNCTIONS
    /// ====================================================================
    /*!
     * \brief treeAnalysis - calls specific tree analysis functions
     *        depending on the mode
     * \param mode - specifies type of tree analysis
     *        0: finding loops \sa treeFindLoops()
     *        1: select from top to bottom \sa treeSelTopDown()
     *        2: select from bottom to top \sa treeSelBottomUp()
     */
    void treeAnalysis(const int& mode);

    /*!
     * \brief treeAdmin - gather user input for tree analysis
     *        the method assigns provided variable references
     *        with meaningful data or marks them as invalid
     * \param model - table model to be analysed
     * \param parIdx - column index denoting the parent id
     * \param childIdx - column index denoting the child id (down id)
     * \param obj - pointer to either NMLayer or NMSqlTableView
     *              hosting the table object
     * \param type - indicating the type of obj pointer
     *               -1: invalid
     *                0: NMLayer*
     *                1: NMSqlTableView*
     *               >1: undefined
     */
    void treeAdmin(QAbstractItemModel*& model, int& parIdx,
                   int& childIdx, void*& obj, int &type);

    /*!
     * \brief processTree - prepares the use of \sa checkTree()
     * \param model
     * \param parIdx
     * \param childIdx
     * \param startId - for mode 1 & 2 only; specifies at which
     *        position in the tree to start the analysis
     * \param endId - specifies any items in the table, which are
     *        outside a valid tree (analysis stops here)
     * \param mode - specifies type of tree analysis
     *        0: finding loops \sa treeFindLoops()
     *        1: select from top to bottom \sa treeSelTopDown()
     *        2: select from bottom to top \sa treeSelBottomUp()
     *
     * \return a sorted list with record IDs (numbers) identified by tree analysis
     */
    QList<int> processTree(QAbstractItemModel*& model, int& parIdx,
                           int& childIdx, int startId, int stopId, int mode);

    /*!
     * \brief checkTree - recursive working horse of tree analysis
     * \param rootId
     * \param idHistory
     * \param treeMap
     * \return
     */
    bool checkTree(const int& rootId, const int& stopId, QList<int>& idHistory,
                   const QMultiMap<int, int> &treeMap);



    void importShapeFile(const QString& filename);

    /*! WebSocket Server start/stop */
    void startWebSocketServer(void);
    void stopWebSocketServer(void);

protected slots:
    void settingsFeeder(QtProperty* prop, const QStringList& strVal);
    void updateSettings(QtProperty* prop, const QVariant& val);
    void updateSettings(const QString& setting, const QVariant& val);

    void updateCursor();
    void populateProcCompList();


    // client & server
    void onNewConnection();
    void onWebSocketServerClosed();
    void onSSlErrors(const QList<QSslError>& errors);
    void processTextMessage(QString message);
    void processBinaryMessage(QByteArray message);
    void socketDisconnected();
    //void onSslErrors(const QList<QSslError>& errors);

    void readProcOutput();

    void onTabifiedDockWidgetActivated(QDockWidget* dockWidget);
    void onDockWidgetAreaChanged(Qt::DockWidgetArea dockArea);
    void setDockWidgetVisibility(QDockWidget *dw, bool bVisible);

protected:

    void hideEvent(QHideEvent* event);
    void showEvent(QShowEvent* event);
    void mousePressEvent(QMouseEvent *event);
    bool eventFilter(QObject *obj, QEvent *event);
    void saveAsImageFile(bool onlyVisImg);
    void closeEvent(QCloseEvent* event);
    void writeSettings(void);
    void populateSettingsBrowser();
    void addModelToUserModelList(const QString& modelName);

    void processUserPickAction(long long cellId, bool bSelection);

    void openTablesReadOnly(void);
    void openTablesReadWrite(void);

    void initWebSocketServer();

    QString eventTypeToString(const QEvent::Type type);

    //	void displayPolyData(vtkSmartPointer<vtkPolyData> polydata, double* lowPt, double* highPt);
#ifndef GDAL_200
    vtkSmartPointer<vtkPolyData> OgrToVtkPolyData(OGRDataSource* pDS);
    void vtkPolygonPolydataToOGR(OGRDataSource* ds, NMVectorLayer *vectorLayer);
#else
    vtkSmartPointer<vtkPolyData> OgrToVtkPolyData(GDALDataset *pDS);
    void vtkPolygonPolydataToOGR(GDALDataset* ds, NMVectorLayer *vectorLayer);
#endif

    // those conversion functions are dealing as well with the particular "Multi-"
    // case (i.e. MultiPoint, etc.)
    vtkSmartPointer<vtkPolyData> wkbPointToPolyData(OGRLayer& l);
    vtkSmartPointer<vtkPolyData> wkbLineStringToPolyData(OGRLayer& l);
    vtkSmartPointer<vtkPolyData> wkbPolygonToPolyData(OGRLayer& l);
    vtkSmartPointer<vtkPolyData> wkbPolygonToTesselatedPolyData(OGRLayer& l);

    /* testing whether pt lies in the cell (2d case)
     * uses ray-casting odd-even rule: i.e. when pt is
     * in the cell, a ray going  through this point
     * intersects with the polygon an odd number of times;
     * and when the point lies outside the polygon, it
     * intersects an even number of times
     * */
    bool ptInPoly2D(double pt[3], vtkCell* cell);

    QString checkMimeDataForModelComponent(const QMimeData *mimedata);

    template<class T>
    void getDoubleFromVtkTypedPtr(T* in, double* out)
    {
        *out = static_cast<double>(*in);
    }

    template<class T>
    void getVtkIdTypeFromVtkTypedPtr(T* in, vtkIdType* out)
    {
        *out = static_cast<vtkIdType>(*in);
    }

    template<class T>
    static int compare_asc(const void* a, const void* b)
    {
        if (*(T*)a < *(T*)b) return -1;
        if (*(T*)a > *(T*)b) return 1;
        return 0;
    }

    template<class T>
    static int compare_desc(const void* a, const void* b)
    {
        if (*(T*)a < *(T*)b) return 1;
        if (*(T*)a > *(T*)b) return -1;
        return 0;
    }

    /** merging two adjacent arrays a and b; a sits in front of b,
     *  and both arrays share the same element type T */
    template<class T>
    static void merge_adj(char* a, char* b, int a_size, int b_size)
    {
        T* aar = reinterpret_cast<T*>(a);
        T* bar = reinterpret_cast<T*>(b);

        long totalcnt = 0;
        long acnt = 0;
        long bcnt = 0;

        while (totalcnt < a_size+b_size)
        {
            if (acnt < a_size && bcnt < b_size)
            {
                if (aar[acnt] >= bar[bcnt])
                {
                    aar[totalcnt] = aar[acnt];
                    ++acnt;
                }
                else
                {
                    aar[totalcnt] = bar[bcnt];
                    ++bcnt;
                }
            }
            else if (acnt < a_size)
            {
                aar[totalcnt] = aar[acnt];
                ++acnt;
            }
            else if (bcnt < b_size)
            {
                aar[totalcnt] = bar[bcnt];
                ++bcnt;
            }
            ++totalcnt;
        }
    }


    inline void quicksort(QList<int>& ar, int left, int right, bool asc)
    {
        int le=left, ri=right, row=0;
        int middle = (le + ri) / 2;

        int mval = ar[middle];

        do
        {
            if (asc)
            {
                while (mval > ar[le]) ++le;
                while (ar[ri] > mval) --ri;
            }
            else
            {
                while (mval < ar[le]) ++le;
                while (ar[ri] < mval) --ri;
            }

            if (le <= ri)
            {
                int t = ar[le];
                ar[le] = ar[ri];
                ar[ri] = t;
                ++le;
                --ri;
            }
        } while (le <= ri);
        if (left < ri) quicksort(ar, left, ri, asc);
        if (right > le) quicksort(ar, le, right, asc);
    }

    void
    offset2index(const long long int& offset,
                 const std::vector<long long int>& sdom, const int& ndim,
                 std::vector<long long int>& idx)
    {
        long long int off = offset;
        for (int d=0; d < ndim; ++d)
        {
            idx[d] = off % sdom[d];
            off /= sdom[d];
        }
    }

    long long int
    index2offset(const std::vector<long long int>& sdom, const int& ndim,
                 const std::vector<long long int>& index)
    {
        long long int offset = 0;
        long long int mult = 1;
        for (int d=ndim-1; d >=0; --d)
        {
            for (int r=d; r >=0; --r)
            {
                if (r == d)
                    mult = index[d];
                else
                    mult *= sdom[r];
            }
            offset += mult;
        }

        return offset;
    }



private:

    // holds the current 3D state (i.e. is interaction
    // in all 3 dims allowed or restricted to 2D)
    bool m_b3D;

    // indicates whether the main window is being
    // loaded for the first time
    bool mbFirstTimeLoaded;

    class OptProc : public QProcess
    {
    public:
        OptProc(QObject* parent=nullptr)
            : QProcess(parent) {}

    protected:
        virtual void setupChildProcess()
        {
            ::setgid(1002);
            ::setuid(1002);
            ::umask(777);
        }
    };


    OptProc* mpLuProc;
    NMMosra* mpMosra;

    // the GUI containing all controls of the main window
    Ui::LUMASSMainWin *ui;
    // for showing the mouse position in real world coordinates
    QLabel* m_coordLabel;
    // for showing pixel values
    QLabel* mPixelValLabel;

    QToolBar* mMapToolBar;
    QIcon mLUMASSIcon;

    QString mLastToolBar;

    QMap<QString, Qt::DockWidgetArea> mDockWidgetAreaMap;

    QMap<QString, QVariant> mSettings;

    QMap<QString, QAction*> mMiscActions;
    QList<QAction*> mExclusiveActions;
    bool mbUpdatingExclusiveActions;

    unsigned int mBusyProcCounter;
    QProgressBar* mProgressBar;

    NMComponentEditor* mTreeCompEditor;
    ModelComponentList* mLayerList;

    // for showing random messages in the status bar
    QLabel* m_StateMsg;

    double mLastPick[3];
    NMLayer* mLastInfoLayer;
    // 0: ascending
    // 1: descending
    // 2: unsorted
    int mInfoTableSortOrder;
    long long mLastInfoTabCellId;

    // the background renderer (layer 0)
    vtkSmartPointer<vtkRenderer> mBkgRenderer;
    // the scale bar renderer (layer n+1)
    vtkSmartPointer<vtkRenderer> mScaleRenderer;

    // connection between vtk and qt event mechanism
    vtkSmartPointer<vtkEventQtSlotConnect> m_vtkConns;
    // orientation marker
    vtkSmartPointer<vtkOrientationMarkerWidget> m_orientwidget;

    vtkSmartPointer<NMVtkInteractorStyleImage> m_iasimg;

    bool mbNoRasdaman;

    // the petascope metadata table
    NMTableView* mpPetaView;
    QAbstractItemModel* mPetaMetaModel;

    QString mSessionDbFileName;
    QString mSessionDbConName;

    // keep track of any imported table data (GIS mode)
    //   (unique) table name
    QMap<QString, QPair<otb::SQLiteTable::Pointer, QSharedPointer<NMSqlTableView> > > mTableList;

    // db path name, view name (linking back to mTableList)
    QMultiMap<QString, QString> mTableDbNames;

    // db path name, connection name
    QMultiMap<QString, QString> mQSqlDbConnectionNameMap;

    QMap<QString, NMAbstractAction*> mUserActions;

    QtTreePropertyBrowser* mSettingsBrowser;

    // keep track of dock widgets
    bool mbComponentInfoDockVisble;
    bool mbComponentsWidgetVisible;
    bool mbLogDockVisible;


    //QListWidget* mTableListWidget;
    NMListWidget* mTableListWidget;

    // USER MODELS
    //QListWidget* mUserModelListWidget;
    NMListWidget* mUserModelListWidget;
    QMap<QString, QString> mUserModelPath;
    QStringList mUserModels;

    QMap<QString, QString> mMapUserToolsModelPath;

    NMLogger* mLogger;
    QObject* mActiveWidget;

    // the last event objects filtered by LUMASSMainWin
    QObject* mLastSender;
    QEvent* mLastEvent;

    //NMModelController* mModelController;

    QWebSocketServer* mServer;
    QList<QWebSocket*> mClientList;

#ifdef BUILD_RASSUPPORT
    RasdamanConnector *mpRasconn;
#endif
};


#endif // LUMASSMainWin_H
