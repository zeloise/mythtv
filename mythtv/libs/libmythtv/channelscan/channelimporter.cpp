// -*- Mode: c++ -*-
/*
 *  Copyright (C) Daniel Kristjansson 2007
 *
 *  This file is licensed under GPL v2 or (at your option) any later version.
 *
 */

// MythTV headers
#include "mythdialogs.h"
#include "mythdb.h"
#include "channelimporter.h"
#include "mpegstreamdata.h" // for kEncDecrypted
#include "channelutil.h"
#include "mythdialogs.h"
#include "mythwidgets.h"

#define LOC QString("ChanImport: ")
#define LOC_ERR QString("ChanImport, Error: ")

static QString map_str(QString str)
{
    if (str.isEmpty())
        return "";
    str.detach();
    return str;
}

void ChannelImporter::Process(const ScanDTVTransportList &_transports)
{
    if (_transports.empty())
    {
        if (use_gui)
        {
            MythPopupBox::showOkPopup(
                gContext->GetMainWindow(), QObject::tr("Channel Importer"),
                ChannelUtil::GetChannelCount()
                ? QObject::tr("Failed to find any new channels!")
                : QObject::tr("Failed to find any channels."));
        }
        else
        {
            VERBOSE(VB_IMPORTANT, LOC + (ChannelUtil::GetChannelCount()
                                         ? "No new channels to process"
                                         : "No channels to process.."));
        }

        return;
    }

    ScanDTVTransportList transports = _transports;

    // Print out each channel
    if (print_verbose_messages | VB_CHANSCAN)
    {
        cout << "Before processing: " << endl;
        ChannelImporterBasicStats infoA = CollectStats(transports);
        cout << FormatChannels(transports, infoA).toAscii().constData() << endl;
        cout << endl << endl;
    }

    uint saved_scan = 0;
    if (do_save)
        saved_scan = SaveScan(transports);

    CleanupDuplicates(transports);

    FilterServices(transports);

    // Pull in DB info
    uint sourceid = transports[0].channels[0].source_id;
    ScanDTVTransportList db_trans = GetDBTransports(sourceid, transports);

    // Make sure "Open Cable" channels are marked that way.
    FixUpOpenCable(transports);

    // Determine System Info standards..
    ChannelImporterBasicStats info = CollectStats(transports);

    // Determine uniqueness of various naming schemes
    ChannelImporterUniquenessStats stats =
        CollectUniquenessStats(transports, info);

    // Print out each channel
    cout << FormatChannels(transports, info).toAscii().constData() << endl;

    // Create summary
    QString msg = GetSummary(transports.size(), info, stats);
    cout << msg.toAscii().constData() << endl << endl;

/*
    if (use_gui)
    {
        MythPopupBox::showOkPopup(
            gContext->GetMainWindow(), QObject::tr("Channel Importer"), msg);
    }
*/

    if (do_insert)
    {
        InsertChannels(transports, info);
        ScanInfo::MarkProcessed(saved_scan);
    }
}

QString ChannelImporter::toString(ChannelType type)
{
    switch (type)
    {
        // non-conflicting
        case kATSCNonConflicting: return "ATSC";
        case kDVBNonConflicting:  return "DVB";
        case kSCTENonConflicting: return "SCTE";
        case kMPEGNonConflicting: return "MPEG";
        case kNTSCNonConflicting: return "NTSC";
        // conflicting
        case kATSCConflicting:    return "ATSC";
        case kDVBConflicting:     return "DVB";
        case kSCTEConflicting:    return "SCTE";
        case kMPEGConflicting:    return "MPEG";
        case kNTSCConflicting:    return "NTSC";
    }
    return "Unknown";
}

void ChannelImporter::InsertChannels(
    const ScanDTVTransportList &transports,
    const ChannelImporterBasicStats &info)
{
    ScanDTVTransportList list = transports;
    ScanDTVTransportList filtered;

    // insert/update all channels with non-conflicting channum
    // and complete tuning information.

    uint chantype = (uint) kChannelTypeNonConflictingFirst;
    for (; chantype <= (uint) kChannelTypeNonConflictingLast; chantype++)
    {
        ChannelType type = (ChannelType) chantype;
        uint new_chan, old_chan;
        CountChannels(list, info, type, new_chan, old_chan);

        if (kNTSCNonConflicting == type)
            continue;

        if (new_chan)
        {
            QString msg = QObject::tr(
                "Found %1 new non-conflicting %2 channels.")
                .arg(new_chan).arg(toString(type));

            InsertAction action = QueryUserInsert(msg);
            list = InsertChannels(list, info, action, type, filtered);
        }
        if (old_chan)
        {
            QString msg = QObject::tr("Found %1 old %2 channels.")
                .arg(old_chan).arg(toString(type));

            UpdateAction action = QueryUserUpdate(msg);
            list = UpdateChannels(list, info, action, type, filtered);
        }
    }

    if (!is_interactive)
        return;

    // sum uniques again
    ChannelImporterBasicStats      ninfo  = CollectStats(list);
    ChannelImporterUniquenessStats nstats = CollectUniquenessStats(list, ninfo);
    cout << endl << endl << "Printing remaining channels" << endl;
    cout << FormatChannels(list, ninfo).toAscii().constData() << endl;
    cout << GetSummary(list.size(), ninfo, nstats).toAscii().constData()
         << endl << endl;

    // if any of the potential uniques is high and inserting
    // with those as the channum would result in few conflicts
    // ask user if it is ok to to proceed using it as the channum

    // for remaining channels with complete tuning information
    // insert channels with contiguous list of numbers as the channums
    chantype = (uint) kChannelTypeConflictingFirst;
    for (; chantype <= (uint) kChannelTypeConflictingLast; chantype++)
    {

        ChannelType type = (ChannelType) chantype;
        uint new_chan, old_chan;
        CountChannels(list, info, type, new_chan, old_chan);
        if (new_chan)
        {
            QString msg = QObject::tr(
                "Found %1 new conflicting %2 channels.")
                .arg(new_chan).arg(toString(type));

            InsertAction action = QueryUserInsert(msg);
            list = InsertChannels(list, info, action, type, filtered);
        }
        if (old_chan)
        {
            QString msg = QObject::tr("Found %1 conflicting old %2 channels.")
                .arg(old_chan).arg(toString(type));

            UpdateAction action = QueryUserUpdate(msg);
            list = UpdateChannels(list, info, action, type, filtered);
        }
    }    

    // if scan was not aborted prematurely
    //   ask user whether to delete all or some of these stale channels
    //     if some is selected ask about each individually
    //   ask user whether to delete all or some of these stale transports
    //     if some is selected ask about each individually

    // print list of inserted channels
    // print list of ignored channels (by ignored reason category)
    // print list of invalid channels
    // print list of stale channels (as deleted if action approved).
}

ScanDTVTransportList ChannelImporter::InsertChannels(
    const ScanDTVTransportList &transports,
    const ChannelImporterBasicStats &info,
    InsertAction action, ChannelType type,
    ScanDTVTransportList &filtered)
{
    QString channelFormat = "%1_%2";

    ScanDTVTransportList next_list;

    bool ignore_rest = false;

    // insert all channels with non-conflicting channum
    // and complete tuning information.
    for (uint i = 0; i < transports.size(); i++)
    {
        bool created_new_transport = false;
        ScanDTVTransport new_transport;
        bool created_filter_transport = false;
        ScanDTVTransport filter_transport;

        for (uint j = 0; j < transports[i].channels.size(); j++)
        {
            ChannelInsertInfo chan = transports[i].channels[j];

            bool filter = false, handle = false;
            if (!chan.channel_id && (kInsertIgnoreAll == action) &&
                IsType(info, chan, type))
            {
                filter = true;
            }
            else if (!chan.channel_id && IsType(info, chan, type))
            {
                handle = true;
            }

            if (ignore_rest)
            {
                cout<<QString("Skipping Insert: %1")
                    .arg(FormatChannel(transports[i], chan))
                    .toAscii().constData()<<endl;
                handle = false;
            }

            if (handle && kInsertManual == action)
            {
                OkCancelType rc = QueryUserInsert(info, transports[i], chan);
                if (kOCTCancelAll == rc)
                {
                    ignore_rest = true;
                    handle = false;
                }
                else if (kOCTCancel == rc)
                {
                    handle = false;
                }
            }

            if (handle)
            {
                bool conflicting = false;

                if (chan.chan_num.isEmpty() ||
                    ChannelUtil::IsConflicting(chan.chan_num, chan.source_id))
                {
                    if ((kATSCNonConflicting == type) ||
                        (kATSCConflicting == type))
                    {
                        chan.chan_num = channelFormat
                            .arg(chan.atsc_major_channel)
                            .arg(chan.atsc_minor_channel);
                    }
                    else
                    {
                        chan.chan_num = QString("%1").arg(chan.service_id);
                    }

                    conflicting = ChannelUtil::IsConflicting(
                        chan.chan_num, chan.source_id);
                }

                if (is_interactive &&
                    (conflicting || (kChannelTypeConflictingFirst <= type)))
                {
                    OkCancelType rc =
                        QueryUserResolve(info, transports[i], chan);

                    conflicting = true;
                    if (kOCTCancelAll == rc)
                        ignore_rest = true;
                    else if (kOCTOk == rc)
                        conflicting = false;
                }

                if (conflicting)
                {
                    cout<<QString("Skipping Insert: %1")
                        .arg(FormatChannel(transports[i], chan))
                        .toAscii().constData()<<endl;
                    handle = false;
                }
            }

            bool inserted = false;
            if (handle)
            {
                int chanid = ChannelUtil::CreateChanID(
                    chan.source_id, chan.chan_num);

                chan.channel_id = (chanid > 0) ? chanid : chan.channel_id;

                if (chan.channel_id && !chan.db_mplexid)
                {
                    uint tsid = chan.vct_tsid;
                    tsid = (tsid) ? tsid : chan.sdt_tsid;
                    tsid = (tsid) ? tsid : chan.pat_tsid;
                    tsid = (tsid) ? tsid : chan.vct_chan_tsid;

                    chan.db_mplexid = ChannelUtil::CreateMultiplex(
                        chan.source_id, transports[i], tsid, chan.orig_netid);
                }

                if (chan.channel_id && chan.db_mplexid)
                {
                    chan.channel_id = chanid;

                    cout<<"Insert("<<chan.si_standard.toAscii().constData()
                        <<"): "<<chan.chan_num.toAscii().constData()<<endl;

                    inserted = ChannelUtil::CreateChannel(
                        chan.db_mplexid,
                        chan.source_id,
                        chan.channel_id,
                        chan.callsign,
                        chan.service_name,
                        chan.chan_num,
                        chan.service_id,
                        chan.atsc_major_channel,
                        chan.atsc_minor_channel,
                        chan.use_on_air_guide,
                        chan.hidden, chan.hidden_in_guide,
                        chan.freqid);
                }
            }

            if (filter)
            {
                if (!created_filter_transport)
                {
                    filter_transport = transports[i];
                    filter_transport.channels.clear();
                    created_filter_transport = true;
                }
                filter_transport.channels.push_back(transports[i].channels[j]);
            }
            else if (!inserted)
            {
                if (!created_new_transport)
                {
                    new_transport = transports[i];
                    new_transport.channels.clear();
                    created_new_transport = true;
                }
                new_transport.channels.push_back(transports[i].channels[j]);
            }
        }

        if (created_filter_transport)
            filtered.push_back(filter_transport);

        if (created_new_transport)
            next_list.push_back(new_transport);
    }

    return next_list;
}

ScanDTVTransportList ChannelImporter::UpdateChannels(
    const ScanDTVTransportList &transports,
    const ChannelImporterBasicStats &info,
    UpdateAction action, ChannelType type,
    ScanDTVTransportList &filtered)
{
    QString channelFormat = "%1_%2";
    bool renameChannels = false;

    ScanDTVTransportList next_list;

    // update all channels with non-conflicting channum
    // and complete tuning information.
    for (uint i = 0; i < transports.size(); i++)
    {
        bool created_transport = false;
        ScanDTVTransport new_transport;
        bool created_filter_transport = false;
        ScanDTVTransport filter_transport;

        for (uint j = 0; j < transports[i].channels.size(); j++)
        {
            ChannelInsertInfo chan = transports[i].channels[j];

            bool filter = false, handle = false;
            if (chan.channel_id && (kUpdateIgnoreAll == action) &&
                IsType(info, chan, type))
            {
                filter = true;
            }
            else if (chan.channel_id && IsType(info, chan, type))
            {
                handle = true;
            }

            if (handle)
            {
                bool conflicting = false;

                if (chan.chan_num.isEmpty() || renameChannels ||
                    ChannelUtil::IsConflicting(
                        chan.chan_num, chan.service_id, chan.channel_id))
                {
                    if (kATSCNonConflicting == type)
                    {
                        chan.chan_num = channelFormat
                            .arg(chan.atsc_major_channel)
                            .arg(chan.atsc_minor_channel);
                    }
                    else
                    {
                        chan.chan_num = QString("%1").arg(chan.service_id);
                    }

                    conflicting = ChannelUtil::IsConflicting(
                        chan.chan_num, chan.service_id, chan.channel_id);
                }

                if (conflicting)
                {
                    cout<<"Skipping Update("
                        <<chan.si_standard.toAscii().constData()<<"): "
                        <<chan.chan_num.toAscii().constData()<<endl;
                    handle = false;
                }
            }

            if (is_interactive && (kUpdateManual == action))
            {
                // TODO Ask user how to update this channel..
            }

            bool updated = false;
            if (handle)
            {
                cout<<"Update("<<chan.si_standard.toAscii().constData()<<"): "
                    <<chan.chan_num.toAscii().constData()<<endl;

                updated = ChannelUtil::UpdateChannel(
                    chan.db_mplexid,
                    chan.source_id,
                    chan.channel_id,
                    chan.callsign,
                    chan.service_name,
                    chan.chan_num,
                    chan.service_id,
                    chan.atsc_major_channel,
                    chan.atsc_minor_channel,
                    chan.use_on_air_guide,
                    chan.hidden, chan.hidden_in_guide,
                    chan.freqid);
            }

            if (filter)
            {
                if (!created_filter_transport)
                {
                    filter_transport = transports[i];
                    filter_transport.channels.clear();
                    created_filter_transport = true;
                }
                filter_transport.channels.push_back(transports[i].channels[j]);
            }
            else if (!updated)
            {
                if (!created_transport)
                {
                    new_transport = transports[i];
                    new_transport.channels.clear();
                    created_transport = true;
                }
                new_transport.channels.push_back(transports[i].channels[j]);
            }
        }

        if (created_filter_transport)
            filtered.push_back(filter_transport);

        if (created_transport)
            next_list.push_back(new_transport);
    }

    return next_list;
}

void ChannelImporter::CleanupDuplicates(ScanDTVTransportList &transports) const
{
    ScanDTVTransportList no_dups;

    DTVTunerType tuner_type = DTVTunerType::kTunerTypeATSC;
    if (!transports.empty())
        tuner_type = transports[0].tuner_type;

    bool is_dvbs =
        (DTVTunerType::kTunerTypeQPSK   == tuner_type) ||
        (DTVTunerType::kTunerTypeDVB_S2 == tuner_type);

    uint freq_mult = (is_dvbs) ? 1 : 1000;

    vector<bool> ignore;
    ignore.resize(transports.size());
    for (uint i = 0; i < transports.size(); i++)
    {
        if (ignore[i])
            continue;

        for (uint j = i+1; j < transports.size(); j++)
        {
            if (!transports[i].IsEqual(
                    tuner_type, transports[j], 500 * freq_mult))
            {
                continue;
            }

            for (uint k = 0; k < transports[j].channels.size(); k++)
            {
                bool found_same = false;
                for (uint l = 0; l < transports[i].channels.size(); l++)
                {
                    if (transports[j].channels[k].IsSameChannel(
                            transports[i].channels[l]))
                    {
                        found_same = true;
                        transports[i].channels[l].ImportExtraInfo(
                            transports[j].channels[k]);
                    }
                }
                if (!found_same)
                    transports[i].channels.push_back(transports[j].channels[k]);
            }
            ignore[j] = true;
        }
        no_dups.push_back(transports[i]);
    }

    transports = no_dups;
}

void ChannelImporter::FilterServices(ScanDTVTransportList &transports) const
{
    for (uint i = 0; i < transports.size(); i++)
    {
        ChannelInsertInfoList filtered;
        for (uint k = 0; k < transports[i].channels.size(); k++)
        {
            if (m_fta_only && transports[i].channels[k].is_encrypted &&
                transports[i].channels[k].decryption_status != kEncDecrypted)
                continue;

            if (transports[i].channels[k].is_data_service)
                continue;

            if (!m_add_radio_services &&
                transports[i].channels[k].is_audio_service)
                continue;

            filtered.push_back(transports[i].channels[k]);
        }
        transports[i].channels = filtered;
    }
}

/** \fn ChannelImporter::GetDBTransports(uint,ScanDTVTransportList&) const
 *  \brief Adds found channel info to transports list,
 *         returns channels in DB which were not found in scan
 */
ScanDTVTransportList ChannelImporter::GetDBTransports(
    uint sourceid, ScanDTVTransportList &transports) const
{
    ScanDTVTransportList not_in_scan;

    DTVTunerType tuner_type = DTVTunerType::kTunerTypeATSC;
    if (!transports.empty())
        tuner_type = transports[0].tuner_type;

    bool is_dvbs =
        (DTVTunerType::kTunerTypeQPSK   == tuner_type) ||
        (DTVTunerType::kTunerTypeDVB_S2 == tuner_type);

    uint freq_mult = (is_dvbs) ? 1 : 1000;

    MSqlQuery query(MSqlQuery::InitCon());
    query.prepare(
        "SELECT mplexid "
        "FROM dtv_multiplex "
        "WHERE sourceid = :SOURCEID "
        "GROUP BY mplexid "
        "ORDER BY mplexid");
    query.bindValue(":SOURCEID", sourceid);

    if (!query.exec())
    {
        MythDB::DBError("GetDBTransports()", query);
        return not_in_scan;
    }

    while (query.next())
    {
        uint mplexid = query.value(0).toUInt();

        ScanDTVTransport newt;
        if (!newt.FillFromDB(tuner_type, mplexid))
            continue;

        bool newt_found = false;
        QMap<uint,bool> found_chan;

        for (uint i = 0; i < transports.size(); i++)
        {
            if (!transports[i].IsEqual(tuner_type, newt, 500 * freq_mult, true))
                continue;

            transports[i].mplex = mplexid;
            newt_found = true;
            for (uint j = 0; j < transports[i].channels.size(); j++)
            {
                ChannelInsertInfo &chan = transports[i].channels[j];
                for (uint k = 0; k < newt.channels.size(); k++)
                {
                    if (newt.channels[k].IsSameChannel(chan))
                    {
                        found_chan[k] = true;
                        chan.db_mplexid = mplexid;
                        chan.channel_id = newt.channels[k].channel_id;
                    }
                }
            }
            break;
        }

        if (!newt_found)
        {
            not_in_scan.push_back(newt);
        }
        else
        {
            ScanDTVTransport tmp = newt;
            tmp.channels.clear();

            for (uint k = 0; k < newt.channels.size(); k++)
            {
                if (!found_chan[k])
                    tmp.channels.push_back(newt.channels[k]);
            }

            if (tmp.channels.size())
                not_in_scan.push_back(tmp);
        }
    }

    return not_in_scan;
}

void ChannelImporter::FixUpOpenCable(ScanDTVTransportList &transports)
{
    ChannelImporterBasicStats info;
    for (uint i = 0; i < transports.size(); i++)
    {
        for (uint j = 0; j < transports[i].channels.size(); j++)
        {
            ChannelInsertInfo &chan = transports[i].channels[j];
            if (((chan.could_be_opencable && (chan.si_standard == "mpeg")) ||
                 chan.is_opencable) && !chan.in_vct)
            {
                chan.si_standard = "opencable";
            }
        }
    }
}

ChannelImporterBasicStats ChannelImporter::CollectStats(
    const ScanDTVTransportList &transports)
{
    ChannelImporterBasicStats info;
    for (uint i = 0; i < transports.size(); i++)
    {
        for (uint j = 0; j < transports[i].channels.size(); j++)
        {
            const ChannelInsertInfo &chan = transports[i].channels[j];
            int enc = (chan.is_encrypted) ?
                ((chan.decryption_status == kEncDecrypted) ? 2 : 1) : 0;
            info.atsc_channels[enc] += (chan.si_standard == "atsc");
            info.dvb_channels [enc] += (chan.si_standard == "dvb");
            info.mpeg_channels[enc] += (chan.si_standard == "mpeg");
            info.scte_channels[enc] += (chan.si_standard == "opencable");
            info.ntsc_channels[enc] += (chan.si_standard == "ntsc");
            if (chan.si_standard != "ntsc")
            {
                info.prognum_cnt[chan.service_id]++;
                info.channum_cnt[map_str(chan.chan_num)]++;
            }
            if (chan.si_standard == "atsc")
            {
                info.atscnum_cnt[(chan.atsc_major_channel << 16) |
                                 (chan.atsc_minor_channel)]++;
                info.atscmin_cnt[chan.atsc_minor_channel]++;
                info.atscmaj_cnt[chan.atsc_major_channel]++;
            }
            if (chan.si_standard == "ntsc")
            {
                info.atscnum_cnt[(chan.atsc_major_channel << 16) |
                                 (chan.atsc_minor_channel)]++;
            }
        }
    }

    return info;
}

ChannelImporterUniquenessStats ChannelImporter::CollectUniquenessStats(
    const ScanDTVTransportList &transports,
    const ChannelImporterBasicStats &info)
{
    ChannelImporterUniquenessStats stats;

    for (uint i = 0; i < transports.size(); i++)
    {
        for (uint j = 0; j < transports[i].channels.size(); j++)
        {
            const ChannelInsertInfo &chan = transports[i].channels[j];
            stats.unique_prognum +=
                (info.prognum_cnt[chan.service_id] == 1) ? 1 : 0;
            stats.unique_channum +=
                (info.channum_cnt[map_str(chan.chan_num)] == 1) ? 1 : 0;

            if (chan.si_standard == "atsc")
            {
                stats.unique_atscnum +=
                    (info.atscnum_cnt[(chan.atsc_major_channel << 16) |
                                 (chan.atsc_minor_channel)] == 1) ? 1 : 0;
                stats.unique_atscmin +=
                    (info.atscmin_cnt[(chan.atsc_minor_channel)] == 1) ? 1 : 0;
                stats.max_atscmajcnt = max(
                    stats.max_atscmajcnt,
                    info.atscmaj_cnt[chan.atsc_major_channel]);
            }
        }
    }

    stats.unique_total = (stats.unique_prognum + stats.unique_atscnum +
                          stats.unique_atscmin + stats.unique_channum);

    return stats;
}


QString ChannelImporter::FormatChannel(
    const ScanDTVTransport          &transport,
    const ChannelInsertInfo         &chan,
    const ChannelImporterBasicStats *info)
{
    QString msg;
    QTextStream ssMsg(&msg);

    ssMsg << transport.modulation.toString().toAscii().constData()
          << ":";
    ssMsg << transport.frequency << ":";

    QString si_standard = (chan.si_standard=="opencable") ?
        QString("scte") : chan.si_standard;

    if (si_standard == "atsc" || si_standard == "scte")
        ssMsg << (QString("%1:%2:%3-%4:%5:%6=%7=%8:%9")
                  .arg(chan.callsign).arg(chan.chan_num)
                  .arg(chan.atsc_major_channel)
                  .arg(chan.atsc_minor_channel)
                  .arg(chan.service_id)
                  .arg(chan.vct_tsid)
                  .arg(chan.vct_chan_tsid)
                  .arg(chan.pat_tsid)
                  .arg(si_standard)).toAscii().constData();
    else if (si_standard == "dvb")
        ssMsg << (QString("%1:%2:%3:%4:%5:%6=%7:%8")
                  .arg(chan.service_name).arg(chan.chan_num)
                  .arg(chan.netid).arg(chan.orig_netid)
                  .arg(chan.service_id)
                  .arg(chan.sdt_tsid)
                  .arg(chan.pat_tsid)
                  .arg(si_standard)).toAscii().constData();
    else
        ssMsg << (QString("%1:%2:%3:%4:%5")
                  .arg(chan.callsign).arg(chan.chan_num)
                  .arg(chan.service_id)
                  .arg(chan.pat_tsid)
                  .arg(si_standard)).toAscii().constData();

    if (info)
    {
        ssMsg <<"\t"
              << chan.channel_id;
    }

    if (info)
    {
        ssMsg << ":"
              << (QString("cnt(pnum:%1,channum:%2)")
                  .arg(info->prognum_cnt[chan.service_id])
                  .arg(info->channum_cnt[map_str(chan.chan_num)])
                  ).toAscii().constData();
        if (chan.si_standard == "atsc")
        {
            ssMsg <<
                (QString(":atsc_cnt(tot:%1,minor:%2)")
                 .arg(info->atscnum_cnt[
                          (chan.atsc_major_channel << 16) |
                          (chan.atsc_minor_channel)])
                 .arg(info->atscmin_cnt[
                          chan.atsc_minor_channel])
                    ).toAscii().constData();
        }
    }

    return msg;
}


QString ChannelImporter::FormatChannels(
    const ScanDTVTransportList      &transports,
    const ChannelImporterBasicStats &info)
{
    QString msg;

    for (uint i = 0; i < transports.size(); i++)
        for (uint j = 0; j < transports[i].channels.size(); j++)
            msg += FormatChannel(transports[i], transports[i].channels[j],
                                 &info) + "\n";

    return msg;
}

QString ChannelImporter::GetSummary(
    uint                                  transport_count,
    const ChannelImporterBasicStats      &info,
    const ChannelImporterUniquenessStats &stats)
{
    
    QString msg = QObject::tr("Found %1 transports:\n").arg(transport_count);
    msg += QObject::tr("Channels: FTA Enc Dec\n") +
        QString("ATSC      %1 %2 %3\n")
        .arg(info.atsc_channels[0],3).arg(info.atsc_channels[1],3)
        .arg(info.atsc_channels[2],3) +
        QString("DVB       %1 %2 %3\n")
        .arg(info.dvb_channels [0],3).arg(info.dvb_channels [1],3)
        .arg(info.dvb_channels [2],3) +
        QString("SCTE      %1 %2 %3\n")
        .arg(info.scte_channels[0],3).arg(info.scte_channels[1],3)
        .arg(info.scte_channels[2],3) +
        QString("MPEG      %1 %2 %3\n")
        .arg(info.mpeg_channels[0],3).arg(info.mpeg_channels[1],3)
        .arg(info.mpeg_channels[2],3) +
        QString("NTSC      %1\n").arg(info.ntsc_channels[0],3) +
        QObject::tr("Unique: prog %1 atsc %2 atsc minor %3 channum %4\n")
        .arg(stats.unique_prognum).arg(stats.unique_atscnum)
        .arg(stats.unique_atscmin).arg(stats.unique_channum) +
        QObject::tr("Max atsc major count: %1")
        .arg(stats.max_atscmajcnt);

    return msg;
}

bool ChannelImporter::IsType(
    const ChannelImporterBasicStats &info,
    const ChannelInsertInfo &chan, ChannelType type)
{
    switch (type)
    {
        case kATSCNonConflicting:
            return ((chan.si_standard == "atsc") &&
                    (info.atscnum_cnt[(chan.atsc_major_channel << 16) |
                                      (chan.atsc_minor_channel)] == 1));

        case kDVBNonConflicting:
            return ((chan.si_standard == "dvb") &&
                    (info.prognum_cnt[chan.service_id] == 1));

        case kMPEGNonConflicting:
            return ((chan.si_standard == "mpeg") &&
                    (info.prognum_cnt[chan.service_id] == 1));

        case kSCTENonConflicting:
            return (((chan.si_standard == "scte") ||
                     (chan.si_standard == "opencable")) &&
                    (info.prognum_cnt[chan.service_id] == 1));

        case kNTSCNonConflicting:
            return ((chan.si_standard == "ntsc") &&
                    (info.atscnum_cnt[(chan.atsc_major_channel << 16) |
                                      (chan.atsc_minor_channel)] == 1));

        case kATSCConflicting:
            return ((chan.si_standard == "atsc") &&
                    (info.atscnum_cnt[(chan.atsc_major_channel << 16) |
                                      (chan.atsc_minor_channel)] != 1));

        case kDVBConflicting:
            return ((chan.si_standard == "dvb") &&
                    (info.prognum_cnt[chan.service_id] != 1));

        case kMPEGConflicting:
            return ((chan.si_standard == "mpeg") &&
                    (info.prognum_cnt[chan.service_id] != 1));

        case kSCTEConflicting:
            return (((chan.si_standard == "scte") ||
                     (chan.si_standard == "opencable")) &&
                    (info.prognum_cnt[chan.service_id] != 1));

        case kNTSCConflicting:
            return ((chan.si_standard == "ntsc") &&
                    (info.atscnum_cnt[(chan.atsc_major_channel << 16) |
                                      (chan.atsc_minor_channel)] != 1));
    }
    return false;
}

void ChannelImporter::CountChannels(
    const ScanDTVTransportList &transports,
    const ChannelImporterBasicStats &info,
    ChannelType type, uint &new_chan, uint &old_chan)
{
    new_chan = old_chan = 0;
    for (uint i = 0; i < transports.size(); i++)
    {
        for (uint j = 0; j < transports[i].channels.size(); j++)
        {
            ChannelInsertInfo chan = transports[i].channels[j];
            if (IsType(info, chan, type))
            {
                if (chan.channel_id)
                    old_chan++;
                else
                    new_chan++;
            }
        }
    }
}

QString ChannelImporter::ComputeSuggestedChannelNum(
    const ChannelImporterBasicStats &info,
    const ScanDTVTransport          &transport,
    const ChannelInsertInfo         &chan)
{
    static QMutex          last_free_lock;
    static QMap<uint,uint> last_free_chan_num_map;

    QString channelFormat = "%1_%2";
    QString chan_num = channelFormat
        .arg(chan.atsc_major_channel)
        .arg(chan.atsc_minor_channel);

    if (!chan.atsc_minor_channel)
        chan_num = QString("%1").arg(chan.service_id);
    
    if (!ChannelUtil::IsConflicting(chan_num, chan.source_id))
        return chan_num;

    QMutexLocker locker(&last_free_lock);
    uint last_free_chan_num = last_free_chan_num_map[chan.source_id];
    for (last_free_chan_num++; ; last_free_chan_num++)
    {
        chan_num = QString::number(last_free_chan_num);
        if (!ChannelUtil::IsConflicting(chan_num, chan.source_id))
            break;
    }
    last_free_chan_num_map[chan.source_id] = last_free_chan_num;

    return chan_num;
}

ChannelImporter::InsertAction
ChannelImporter::QueryUserInsert(const QString &msg)
{
    InsertAction action = kInsertAll;
    if (use_gui)
    {
        QStringList buttons;
        buttons.push_back(QObject::tr("Insert all"));
        buttons.push_back(QObject::tr("Insert manually"));
        buttons.push_back(QObject::tr("Ignore all"));

        DialogCode ret;
        do
        {
            ret = MythPopupBox::ShowButtonPopup(
                gContext->GetMainWindow(), QObject::tr("Channel Importer"),
                msg, buttons, kDialogCodeButton0);

            ret = (kDialogCodeRejected == ret) ? kDialogCodeButton2 : ret;

        } while (!(kDialogCodeButton0 <= ret && ret <= kDialogCodeButton2));

        action = (kDialogCodeButton0 == ret) ? kInsertAll       : action;
        action = (kDialogCodeButton1 == ret) ? kInsertManual    : action;
        action = (kDialogCodeButton2 == ret) ? kInsertIgnoreAll : action;
    }
    else if (is_interactive)
    {
        cout << msg.toAscii().constData()          << endl;
        cout << "Do you want to:"    << endl;
        cout << "1. Insert all"      << endl;
        cout << "2. Insert manually" << endl;
        cout << "3. Ignore all"      << endl;
        while (true)
        {
            string ret;
            cin >> ret;
            bool ok;
            uint val = QString(ret.c_str()).toUInt(&ok);
            if (ok && (1 <= val) && (val <= 3))
            {
                action = (1 == val) ? kInsertAll       : action;
                action = (2 == val) ? kInsertManual    : action;
                action = (3 == val) ? kInsertIgnoreAll : action;
                break;
            }
            else
            {
                cout << "Please enter either 1, 2, or 3:" << endl;
            }
        }
    }

    return action;
}

ChannelImporter::UpdateAction
ChannelImporter::QueryUserUpdate(const QString &msg)
{
    UpdateAction action = kUpdateAll;

    if (use_gui)
    {
        QStringList buttons;
        buttons.push_back(QObject::tr("Update all"));
        buttons.push_back(QObject::tr("Update manually"));
        buttons.push_back(QObject::tr("Ignore all"));

        DialogCode ret;
        do
        {
            ret = MythPopupBox::ShowButtonPopup(
                gContext->GetMainWindow(), QObject::tr("Channel Importer"),
                msg, buttons, kDialogCodeButton0);

            ret = (kDialogCodeRejected == ret) ? kDialogCodeButton2 : ret;

        } while (!(kDialogCodeButton0 <= ret && ret <= kDialogCodeButton2));

        action = (kDialogCodeButton0 == ret) ? kUpdateAll       : action;
        action = (kDialogCodeButton1 == ret) ? kUpdateManual    : action;
        action = (kDialogCodeButton2 == ret) ? kUpdateIgnoreAll : action;
    }
    else if (is_interactive)
    {
        cout << msg.toAscii().constData()
             << endl
             << QObject::tr("Do you want to:").toAscii().constData()
             << endl
             << "1. " << QObject::tr("Update all").toAscii().constData()
             << endl
             << "2. " << QObject::tr("Update manually").toAscii().constData()
             << endl
             << "3. " << QObject::tr("Ignore all").toAscii().constData()
             << endl;
        while (true)
        {
            string ret;
            cin >> ret;
            bool ok;
            uint val = QString(ret.c_str()).toUInt(&ok);
            if (ok && (1 <= val) && (val <= 3))
            {
                action = (1 == val) ? kUpdateAll       : action;
                action = (2 == val) ? kUpdateManual    : action;
                action = (3 == val) ? kUpdateIgnoreAll : action;
                break;
            }
            else
            {
                cout << QObject::tr(
                    "Please enter either 1, 2, or 3:")
                    .toAscii().constData() << endl;
            }
        }
    }

    return action;
}

OkCancelType ChannelImporter::ShowManualChannelPopup(
    MythMainWindow *parent, QString title,
    QString message, QString &text)
{
    MythPopupBox *popup = new MythPopupBox(parent, title.toAscii().constData());

    popup->addLabel(message, MythPopupBox::Medium, true);

    MythLineEdit *textEdit = new MythLineEdit(popup);

    QString orig_text = text;
    text = "";
    textEdit->setText(text);
    popup->addWidget(textEdit);

    popup->addButton(QObject::tr("OK"),     popup, SLOT(accept()));
    popup->addButton(QObject::tr("Suggest"));
    popup->addButton(QObject::tr("Cancel"), popup, SLOT(reject()));
    popup->addButton(QObject::tr("Cancel All"));

    textEdit->setFocus();

    DialogCode dc = popup->ExecPopup();
    if (kDialogCodeButton1 == dc)
    {
        popup->hide();
        popup->deleteLater();

        popup = new MythPopupBox(parent, title.toAscii().constData());
        popup->addLabel(message, MythPopupBox::Medium, true);

        textEdit = new MythLineEdit(popup);

        text = orig_text;
        textEdit->setText(text);
        popup->addWidget(textEdit);

        popup->addButton(QObject::tr("OK"), popup, SLOT(accept()))->setFocus();
        popup->addButton(QObject::tr("Cancel"), popup, SLOT(reject()));
        popup->addButton(QObject::tr("Cancel All"));

        dc = popup->ExecPopup();
    }

    bool ok = (kDialogCodeAccepted == dc);
    if (ok)
        text = textEdit->text();

    popup->hide();
    popup->deleteLater();

    return (ok) ? kOCTOk :
        ((kDialogCodeRejected == dc) ? kOCTCancel : kOCTCancelAll);
}

OkCancelType ChannelImporter::QueryUserResolve(
    const ChannelImporterBasicStats &info,
    const ScanDTVTransport          &transport,
    ChannelInsertInfo               &chan)
{
    QString msg = QObject::tr(
        "This channel '%1' was found to be in conflict with other channels. ")
        .arg(FormatChannel(transport, chan));

    OkCancelType ret = kOCTCancel;

    if (use_gui)
    {
        while (true)
        {
            QString msg2 = msg;
            msg2 += QObject::tr("Please enter a unique channel number. ");

            QString val = ComputeSuggestedChannelNum(info, transport, chan);
            ret = ShowManualChannelPopup(
                gContext->GetMainWindow(), QObject::tr("Channel Importer"),
                msg2, val);

            if (kOCTOk != ret)
                break; // user canceled..

            bool ok = (val.length() >= 1);
            ok = ok && ((val[0] >= '0') && (val[0] <= '9'));
            ok = ok && !ChannelUtil::IsConflicting(
                val, chan.source_id, chan.channel_id);

            chan.chan_num = (ok) ? val : chan.chan_num;
            if (ok)
                break;
        }
    }
    else if (is_interactive)
    {
        cout << msg.toAscii().constData() << endl;

        QString cancelStr = QObject::tr("Cancel").toLower();
        QString cancelAllStr = QObject::tr("Cancel All").toLower();
        QString msg2 = QObject::tr(
            "Please enter a non-conflicting channel number "
            "(or type %1 to skip, %2 to skip all): ")
            .arg(cancelStr).arg(cancelAllStr);

        while (true)
        {
            cout << msg2.toAscii().constData() << endl;
            string sret;
            cin >> sret;
            QString val = QString(sret.c_str());
            if (val.toLower() == cancelStr)
            {
                ret = kOCTCancel;
                break; // user canceled..
            }
            if (val.toLower() == cancelAllStr)
            {
                ret = kOCTCancelAll;
                break; // user canceled..
            }

            bool ok = (val.length() >= 1);
            ok = ok && ((val[0] >= '0') && (val[0] <= '9'));
            ok = ok && !ChannelUtil::IsConflicting(
                val, chan.source_id, chan.channel_id);

            chan.chan_num = (ok) ? val : chan.chan_num;
            if (ok)
            {
                ret = kOCTOk;
                break;
            }
        }
    }

    return ret;
}

OkCancelType ChannelImporter::QueryUserInsert(
    const ChannelImporterBasicStats &info,
    const ScanDTVTransport          &transport,
    ChannelInsertInfo               &chan)
{
    QString msg = QObject::tr(
        "You chose to manually insert this channel '%1'.")
        .arg(FormatChannel(transport, chan));

    OkCancelType ret = kOCTCancel;

    if (use_gui)
    {
        while (true)
        {
            QString msg2 = msg;
            msg2 += QObject::tr("Please enter a unique channel number. ");

            QString val = ComputeSuggestedChannelNum(info, transport, chan);
            ret = ShowManualChannelPopup(
                gContext->GetMainWindow(), QObject::tr("Channel Importer"),
                msg2, val);

            if (kOCTOk != ret)
                break; // user canceled..

            bool ok = (val.length() >= 1);
            ok = ok && ((val[0] >= '0') && (val[0] <= '9'));
            ok = ok && !ChannelUtil::IsConflicting(
                val, chan.source_id, chan.channel_id);

            chan.chan_num = (ok) ? val : chan.chan_num;
            if (ok)
            {
                ret = kOCTOk;
                break;
            }
        }
    }
    else if (is_interactive)
    {
        cout << msg.toAscii().constData() << endl;

        QString cancelStr    = QObject::tr("Cancel").toLower();
        QString cancelAllStr = QObject::tr("Cancel All").toLower();
        QString msg2 = QObject::tr(
            "Please enter a non-conflicting channel number "
            "(or type %1 to skip, %2 to skip all): ")
            .arg(cancelStr).arg(cancelAllStr);

        while (true)
        {
            cout << msg2.toAscii().constData() << endl;
            string sret;
            cin >> sret;
            QString val = QString(sret.c_str());
            if (val.toLower() == cancelStr)
            {
                ret = kOCTCancel;
                break; // user canceled..
            }
            if (val.toLower() == cancelAllStr)
            {
                ret = kOCTCancelAll;
                break; // user canceled..
            }

            bool ok = (val.length() >= 1);
            ok = ok && ((val[0] >= '0') && (val[0] <= '9'));
            ok = ok && !ChannelUtil::IsConflicting(
                val, chan.source_id, chan.channel_id);

            chan.chan_num = (ok) ? val : chan.chan_num;
            if (ok)
            {
                ret = kOCTOk;
                break;
            }
        }
    }

    return ret;
}