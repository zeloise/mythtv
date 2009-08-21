// -*- Mode: c++ -*-
/*
 *  Copyright (C) Daniel Kristjansson 2007
 *
 *  This file is licensed under GPL v2 or (at your option) any later version.
 *
 */

#ifndef _CHANNEL_IMPORTER_H_
#define _CHANNEL_IMPORTER_H_

// ANSI C headers
#include <string.h>

// Qt headers
#include <QMap>
#include <QString>

// MythTV headers
#include "mythexp.h"
#include "scaninfo.h"

typedef enum {
    kOCTCancelAll = -1,
    kOCTCancel    = +0,
    kOCTOk        = +1,
} OkCancelType;

class ChannelImporterBasicStats
{
  public:
    ChannelImporterBasicStats()
    {
        memset(atsc_channels, 0, sizeof(atsc_channels));
        memset(dvb_channels,  0, sizeof(dvb_channels));
        memset(scte_channels, 0, sizeof(scte_channels));
        memset(mpeg_channels, 0, sizeof(mpeg_channels));
        memset(ntsc_channels, 0, sizeof(ntsc_channels));
    }

    // totals
    uint atsc_channels[3];
    uint dvb_channels [3];
    uint scte_channels[3];
    uint mpeg_channels[3];
    uint ntsc_channels[3];

    // per channel counts
    QMap<uint,uint>    prognum_cnt;
    QMap<uint,uint>    atscnum_cnt;
    QMap<uint,uint>    atscmin_cnt;
    QMap<uint,uint>    atscmaj_cnt;
    QMap<QString,uint> channum_cnt;
};

class ChannelImporterUniquenessStats
{
  public:
    ChannelImporterUniquenessStats() :
        unique_prognum(0), unique_atscnum(0),
        unique_atscmin(0), unique_channum(0),
        unique_total(0),   max_atscmajcnt(0)
    {
    }

    uint unique_prognum;
    uint unique_atscnum;
    uint unique_atscmin;
    uint unique_channum;
    uint unique_total;
    uint max_atscmajcnt;
};

class MPUBLIC ChannelImporter
{
  public:
    ChannelImporter(bool gui, bool interactive, bool insert, bool save,
                    bool only_fta, bool add_radio_services) :
        use_gui(gui), is_interactive(interactive),
        do_insert(insert), do_save(save), m_fta_only(only_fta),
        m_add_radio_services(add_radio_services) { }

    void Process(const ScanDTVTransportList&);

  protected:
    typedef enum
    {
        kInsertAll,
        kInsertManual,
        kInsertIgnoreAll,
    } InsertAction;
    typedef enum
    {
        kUpdateAll,
        kUpdateManual,
        kUpdateIgnoreAll,
    } UpdateAction;

    typedef enum
    {
        kChannelTypeFirst = 0,

        kChannelTypeNonConflictingFirst = kChannelTypeFirst,
        kATSCNonConflicting = kChannelTypeFirst,
        kDVBNonConflicting,
        kSCTENonConflicting,
        kMPEGNonConflicting,
        kNTSCNonConflicting,
        kChannelTypeNonConflictingLast = kNTSCNonConflicting,

        kChannelTypeConflictingFirst,
        kATSCConflicting = kChannelTypeConflictingFirst,
        kDVBConflicting,
        kSCTEConflicting,
        kMPEGConflicting,
        kNTSCConflicting,
        kChannelTypeConflictingLast = kNTSCConflicting,
        kChannelTypeLast = kChannelTypeConflictingLast,
    } ChannelType;

    QString toString(ChannelType type);

    void CleanupDuplicates(ScanDTVTransportList &transports) const;
    void FilterServices(ScanDTVTransportList &transports) const;
    ScanDTVTransportList GetDBTransports(
        uint sourceid, ScanDTVTransportList&) const;

    void InsertChannels(const ScanDTVTransportList&,
                        const ChannelImporterBasicStats&);

    ScanDTVTransportList InsertChannels(
        const ScanDTVTransportList &transports,
        const ChannelImporterBasicStats &info,
        InsertAction action, ChannelType type,
        ScanDTVTransportList &filtered);

    ScanDTVTransportList UpdateChannels(
        const ScanDTVTransportList &transports,
        const ChannelImporterBasicStats &info,
        UpdateAction action, ChannelType type,
        ScanDTVTransportList &filtered);

    /// For a multiple channels
    InsertAction QueryUserInsert(const QString &msg);

    /// For a multiple channels
    UpdateAction QueryUserUpdate(const QString &msg);

    /// For a single channel
    OkCancelType QueryUserResolve(
        const ChannelImporterBasicStats &info,
        const ScanDTVTransport          &transport,
        ChannelInsertInfo               &chan);

    /// For a single channel
    OkCancelType QueryUserInsert(
        const ChannelImporterBasicStats &info,
        const ScanDTVTransport          &transport,
        ChannelInsertInfo               &chan);

    static QString ComputeSuggestedChannelNum(
        const ChannelImporterBasicStats &info,
        const ScanDTVTransport          &transport,
        const ChannelInsertInfo         &chan);

    static OkCancelType ShowManualChannelPopup(
        MythMainWindow *parent, QString title,
        QString message, QString &text);

    static void FixUpOpenCable(ScanDTVTransportList &transports);

    static ChannelImporterBasicStats CollectStats(
        const ScanDTVTransportList &transports);

    static ChannelImporterUniquenessStats CollectUniquenessStats(
        const ScanDTVTransportList &transports,
        const ChannelImporterBasicStats &info);

    static QString FormatChannels(
        const ScanDTVTransportList      &transports,
        const ChannelImporterBasicStats &info);

    static QString FormatChannel(
        const ScanDTVTransport          &transport,
        const ChannelInsertInfo         &chan,
        const ChannelImporterBasicStats *info = NULL);

    static QString GetSummary(
        uint                                  transport_count,
        const ChannelImporterBasicStats      &info,
        const ChannelImporterUniquenessStats &stats);

    static bool IsType(
        const ChannelImporterBasicStats &info,
        const ChannelInsertInfo &chan, ChannelType type);

    static void CountChannels(
        const ScanDTVTransportList &transports,
        const ChannelImporterBasicStats &info,
        ChannelType type, uint &new_chan, uint &old_chan);

  private:
    bool use_gui;
    bool is_interactive;
    bool do_insert;
    bool do_save;
    bool m_fta_only;
    bool m_add_radio_services;
};

#endif // _CHANNEL_IMPORTER_H_