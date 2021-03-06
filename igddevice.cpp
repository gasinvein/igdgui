#include "igddevice.h"
#include <QString>
#include <cstring>
#include <miniupnpc/upnperrors.h>

ScanThread::ScanThread(QObject *parent, UPNPDev*& devlist, QMutex& devlist_mutex) : QThread(parent), devlist(devlist), devlist_mutex(devlist_mutex) {}

void ScanThread::run() {
  int upnp_error;
  UPNPDev* my_devlist;

  my_devlist = upnpDiscover(2000, NULL, UPNP_LOCAL_PORT_ANY, 0, 0, 2, &upnp_error);

  devlist_mutex.lock();
  if(devlist != NULL)
    freeUPNPDevlist(devlist);
  devlist = my_devlist;
  devlist_mutex.unlock();
}

IGDDevice::IGDDevice(QObject *parent) : QObject(parent), hasValidIGD(false), connected(false), devlist(NULL) {
  scanner = new ScanThread(this, devlist, devlist_mutex);
  connect( scanner, SIGNAL( finished() ), this, SIGNAL( IgdScanFinished() ) );
  connect( scanner, SIGNAL( finished() ), this, SLOT( refresh() ) );

  mForwardData = new QStandardItemModel(this);
  mForwardData->setSortRole(PortProtoSortRole);
}

IGDDevice::~IGDDevice() {
  if(scanner != NULL)
    scanner->wait();
  if(devlist != NULL)
    freeUPNPDevlist(devlist);
  if(hasValidIGD)
    FreeUPNPUrls(&urls);
}

void IGDDevice::scan() {
  emit IgdStartScan();
  if(scanner->isRunning())
    return;
  scanner->start();
}

void IGDDevice::refresh() {
  int i;

  if(hasValidIGD)
    FreeUPNPUrls(&urls);
  mForwardData->clear();
  hasValidIGD = false;
  connected = false;

  devlist_mutex.lock();
  i = UPNP_GetValidIGD(devlist, &urls, &data, lanaddr, sizeof(lanaddr));
  devlist_mutex.unlock();
  if(i!=0) {
    if(i==3) {
      FreeUPNPUrls(&urls);
    } else {
      hasValidIGD = true;
      if(UPNP_GetExternalIPAddress(urls.controlURL, data.first.servicetype, externalip) != 0)
        externalip[0] = '\0';
      if(UPNPIGD_IsConnected(&urls, &data))
        connected = true;
      readPortMappingsIntoModel();
    }
  }
  emit IgdDataRefreshed();
}

void IGDDevice::readPortMappingsIntoModel() {
  int r, i=0;
  QString index;
  char extPort[6], intClient[40], intPort[6], protocol[4], description[80], enabled[6], rHost[64], duration[16];
  QVariant ExtPort, IntClient, IntPort, Protocol, Description;
  QStandardItem *q;

  do {
    index = QString::number(i++);
    extPort[0] = '\0';
    intClient[0] = '\0';
    intPort[0] = '\0';
    protocol[0] = '\0';
    description[0] = '\0';
    enabled[0] = '\0';
    rHost[0] = '\0';
    duration[0] = '\0';
    r = UPNP_GetGenericPortMappingEntry(urls.controlURL, data.first.servicetype, index.toLatin1().constData(), extPort,
                                        intClient, intPort, protocol, description, enabled, rHost, duration);
      if(r==0) {
        q = new QStandardItem;

        ExtPort = QVariant(extPort);
        ExtPort.convert(QVariant::UInt);
        q->setData(ExtPort, ExternalPortRole);
        IntClient = QVariant(intClient);
        q->setData(IntClient, InternalIpRole);
        IntPort = QVariant(intPort);
        IntPort.convert(QVariant::UInt);
        q->setData(IntPort, InternalPortRole);
        if(strcmp(protocol, "TCP") == 0)
          Protocol = QVariant(ProtoTcp);
        else if(strcmp(protocol, "UDP") == 0)
          Protocol = QVariant(ProtoUdp);
        else
          Protocol = QVariant(ProtoInvalid);
        q->setData(Protocol, ProtocolRole);
        Description = QVariant(description);
        q->setData(Description, DescriptionRole);
        q->setData(enabled, EnabledRole);
        q->setData(rHost, RemoteHostRole);
        q->setData(duration, DurationRole);
        q->setData(ExtPort.toUInt()*2 + Protocol.toUInt(), PortProtoSortRole);
        q->setData(Description.toString() + " (" + protocol + " " + ExtPort.toString() + "->" + IntClient.toString() + ":" + IntPort.toString() + ")", Qt::DisplayRole);
        q->setEditable(false);
        mForwardData->appendRow(q);
      }
  } while(r==0);
  mForwardData->sort(0);
}

bool IGDDevice::deletePortMapping(QString &error, QStandardItem *m) {
  int r;
  uint p;
  const char *pStr;

  if(!validIGD())
    return false;
  p = m->data(ProtocolRole).toUInt();
  if(p == ProtoTcp)
    pStr = "TCP";
  else if(p == ProtoUdp)
    pStr = "UDP";
  else
    return false;
  r = UPNP_DeletePortMapping(urls.controlURL, data.first.servicetype, m->data(ExternalPortRole).toString().toLatin1().constData(), pStr, NULL);
  if(!r) {
    mForwardData->clear();
    readPortMappingsIntoModel();
  } else {
    error.sprintf("%d: %s", r, strupnperror(r));
  }
  return !r;
}

bool IGDDevice::addPortMappingFromStrings(QString &error, const QString &ExtPort, const QString &Protocol, const QString &IntPort, const QString &IntClient, const QString &Description) {
  int r;
  r = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype,
                          ExtPort.toLatin1().constData(),
                          IntPort.toLatin1().constData(),
                          IntClient.toLatin1().constData(),
                          Description.toLatin1().constData(),
                          Protocol.toLatin1().constData(),
                          NULL,
                          NULL);
  if(!r) {
    mForwardData->clear();
    readPortMappingsIntoModel();
  } else {
    error.sprintf("%d: %s", r, strupnperror(r));
  }
  return !r;
}
