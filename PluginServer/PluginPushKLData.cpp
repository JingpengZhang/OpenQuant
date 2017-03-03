#include "stdafx.h"
#include "PluginPushKLData.h"
#include "PluginQuoteServer.h"
#include "Protocol/ProtoPushKLData.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#define TIMER_ID_CLEAR_CACHE		354
#define TIMER_ID_HANDLE_TIMEOUT_REQ	355

#define EVENT_ID_ACK_REQUEST	368

//tomodify 2
#define PROTO_ID_QUOTE		PROTO_ID_PUSH_KLDATA
#define QUOTE_SERVER_TYPE	QuoteServer_PushKLData
typedef CProtoPushKLData		CProtoQuote;


//////////////////////////////////////////////////////////////////////////

CPluginPushKLData::CPluginPushKLData()
{	
	m_pQuoteData = NULL;
	m_pQuoteServer = NULL;

	m_bStartTimerClearCache = FALSE;
	m_bStartTimerHandleTimeout = FALSE;
}

CPluginPushKLData::~CPluginPushKLData()
{
	Uninit();
}

void CPluginPushKLData::Init(CPluginQuoteServer* pQuoteServer, IFTQuoteData*  pQuoteData)
{
	if ( m_pQuoteServer != NULL )
		return;

	if ( pQuoteServer == NULL || pQuoteData == NULL )
	{
		ASSERT(false);
		return;
	}

	m_pQuoteServer = pQuoteServer;
	m_pQuoteData = pQuoteData;
	m_TimerWnd.SetEventInterface(this);
	m_TimerWnd.Create();

	m_MsgHandler.SetEventInterface(this);
	m_MsgHandler.Create();
}

void CPluginPushKLData::Uninit()
{
	if ( m_pQuoteServer != NULL )
	{
		m_pQuoteServer = NULL;
		m_pQuoteData = NULL;
		m_TimerWnd.Destroy();
		m_TimerWnd.SetEventInterface(NULL);

		m_MsgHandler.Close();
		m_MsgHandler.SetEventInterface(NULL);

		ClearAllReqCache();
	}
}

void CPluginPushKLData::SetQuoteReqData(int nCmdID, const Json::Value &jsnVal, SOCKET sock)
{
	CHECK_RET(nCmdID == PROTO_ID_QUOTE && sock != INVALID_SOCKET, NORET);
	CHECK_RET(m_pQuoteData && m_pQuoteServer, NORET);

	CProtoQuote proto;
	CProtoQuote::ProtoReqDataType	req;
	proto.SetProtoData_Req(&req);
	if ( !proto.ParseJson_Req(jsnVal) )
	{
		CHECK_OP(false, NORET);
		StockDataReq req_info;
		req_info.sock = sock;
		req_info.req = req;
		ReplyDataReqError(&req_info, PROTO_ERR_PARAM_ERR, L"��������");
		return;
	}

	CHECK_RET(req.head.nProtoID == nCmdID, NORET);

	std::wstring strCode;
	CA::UTF2Unicode(req.body.strStockCode.c_str(), strCode);
	INT64 nStockID = m_pQuoteData->GetStockHashVal(strCode.c_str(), (StockMktType)req.body.nStockMarket);
	if ( nStockID == 0 )
	{
		CHECK_OP(false, NOOP);
		StockDataReq req_info;
		req_info.nStockID = nStockID;
		req_info.sock = sock;
		req_info.req = req;
		ReplyDataReqError(&req_info, PROTO_ERR_STOCK_NOT_FIND, L"�Ҳ�����Ʊ��");
		return;
	}	

	if ( req.body.nKLType <= 0 || req.body.nKLType == 5 || req.body.nKLType >= 10)
	{
		//////��������
		StockDataReq req_info;
		req_info.nStockID = nStockID;
		req_info.sock = sock;
		req_info.req = req;
		ReplyDataReqError(&req_info, PROTO_ERR_PARAM_ERR, L"��������");
		return;
	}

	if ( req.body.nRehabType < 0 || req.body.nRehabType > 2 )
	{
		//////��������
		StockDataReq req_info;
		req_info.nStockID = nStockID;
		req_info.sock = sock;
		req_info.req = req;
		ReplyDataReqError(&req_info, PROTO_ERR_PARAM_ERR, L"��������");
		return;
	}

	if ( req.body.nNum < 1 || req.body.nRehabType > 1000 )
	{
		//////��������
		StockDataReq req_info;
		req_info.nStockID = nStockID;
		req_info.sock = sock;
		req_info.req = req;
		ReplyDataReqError(&req_info, PROTO_ERR_PARAM_ERR, L"��������");
		return;
	}

	if ( m_mapStockIDCode.find(nStockID) == m_mapStockIDCode.end() )
	{
		StockMktCode &mkt_code = m_mapStockIDCode[nStockID];
		mkt_code.nMarketType = req.body.nStockMarket;
		mkt_code.strCode = req.body.strStockCode;
	}

	StockDataReq *pReqInfo = new StockDataReq;
	CHECK_RET(pReqInfo, NORET);
	pReqInfo->nStockID = nStockID;
	pReqInfo->sock = sock;
	pReqInfo->req = req;
	pReqInfo->dwLocalCookie = 0;
	pReqInfo->dwReqTick = ::GetTickCount();

	VT_STOCK_DATA_REQ &vtReq = m_mapReqInfo[std::make_pair(nStockID, req.body.nKLType)];
	bool bNeedSub = vtReq.empty();	
	vtReq.push_back(pReqInfo);

	if ( bNeedSub )
	{
		bool bIsSub = false;
		switch(req.body.nKLType)
		{
		case 1:
			bIsSub = m_pQuoteData->IsSubStockOneType(nStockID, StockSubType_KL_MIN1);
			break;

		case 2:
			bIsSub = m_pQuoteData->IsSubStockOneType(nStockID, StockSubType_KL_DAY);
			break;

		case 3:
			bIsSub = m_pQuoteData->IsSubStockOneType(nStockID, StockSubType_KL_WEEK);
			break;

		case 4:
			bIsSub = m_pQuoteData->IsSubStockOneType(nStockID, StockSubType_KL_MONTH);
			break;

		case 6:
			bIsSub = m_pQuoteData->IsSubStockOneType(nStockID, StockSubType_KL_MIN5);
			break;

		case 7:
			bIsSub = m_pQuoteData->IsSubStockOneType(nStockID, StockSubType_KL_MIN15);
			break;

		case 8:
			bIsSub = m_pQuoteData->IsSubStockOneType(nStockID, StockSubType_KL_MIN30);
			break;

		case 9:
			bIsSub = m_pQuoteData->IsSubStockOneType(nStockID, StockSubType_KL_MIN60);
			break;

		}

		if ( bIsSub )
		{
			if ( m_pQuoteData->IsKLDataExist(nStockID, req.body.nKLType) )
			{
				//tomodify 3.1
				std::map<int, QuoteAckDataBody> &ackmap = m_mapCacheData[std::make_pair(nStockID, req.body.nKLType)];
				ackmap.clear();
				for ( int i = 0; i <= 2; i++)
				{
					Quote_StockKLData* pQuoteKL = NULL;
					int nCount = 0;
					if (m_pQuoteData->FillKLData(nStockID, pQuoteKL, nCount, req.body.nKLType, i))
					{
						QuoteAckDataBody &ack = ackmap[i];
						ack.nKLType = req.body.nKLType;
						ack.nRehabType = i;
						ack.nStockMarket = req.body.nStockMarket;
						ack.strStockCode = req.body.strStockCode;
						ack.vtKLData.clear();
						for ( int n = 0; n < nCount; n++ )
						{
							PushKLDataAckItem item;
							wchar_t szTime[64] = {}; 
							m_pQuoteData->TimeStampToStr(nStockID, pQuoteKL[n].dwTime,szTime);
							item.strTime = szTime;
							item.nOpenPrice = pQuoteKL[n].nOpenPrice;
							item.nClosePrice = pQuoteKL[n].nClosePrice;
							item.nHighestPrice = pQuoteKL[n].nHighestPrice;
							item.nLowestPrice = pQuoteKL[n].nLowestPrice;
							item.nPERatio= pQuoteKL[n].nPERatio;
							item.nTurnoverRate = pQuoteKL[n].nTurnoverRate;
							item.ddwTDVol= pQuoteKL[n].ddwTDVol;
							item.ddwTDVal = pQuoteKL[n].ddwTDVal;
							ack.vtKLData.push_back(item);
						}
					}
					m_pQuoteData->DeleteKLDataPointer(pQuoteKL);
				}
				m_MsgHandler.RaiseEvent(EVENT_ID_ACK_REQUEST, 0, 0);
			}
			else
			{
				QueryDataErrCode err_code = m_pQuoteServer->QueryStockKLData((DWORD*)&pReqInfo->dwLocalCookie, pReqInfo->req.body.strStockCode, (StockMktType)pReqInfo->req.body.nStockMarket, QUOTE_SERVER_TYPE, req.body.nKLType);
				if (err_code != QueryData_Suc)
				{
					ReplyDataReqError(&(*pReqInfo), PROTO_ERR_UNKNOWN_ERROR, L"��ȡ����ʧ�ܣ�");
					MAP_STOCK_DATA_REQ::iterator it_stock = m_mapReqInfo.begin();
					for ( ; it_stock != m_mapReqInfo.end(); )
					{
						INT64 nStockID2 = it_stock->first.first;
						int nKLType = it_stock->first.second;
						if ( nStockID2 == nStockID && nKLType == req.body.nKLType)
						{
							it_stock = m_mapReqInfo.erase(it_stock);
						}
					}
					return;
				}
			}
			SetTimerHandleTimeout(true);
		}
		else
		{
			////��vtReq�е�ÿһ��
			for (size_t i = 0; i < vtReq.size(); i++)
			{
				StockDataReq *pReqAnswer = vtReq[i];
				ReplyDataReqError(pReqAnswer, PROTO_ERR_UNSUB_ERR, L"��Ʊδ���ģ�");
			}
			MAP_STOCK_DATA_REQ::iterator it_iterator = m_mapReqInfo.find(std::make_pair(nStockID, req.body.nKLType));
			if ( it_iterator != m_mapReqInfo.end() )
			{
				it_iterator = m_mapReqInfo.erase(it_iterator);
			}
			return;
		}
	}
}

void CPluginPushKLData::NotifyQuoteDataUpdate(int nCmdID, INT64 nStockID, int nKLType)
{
	CHECK_RET(nCmdID == PROTO_ID_QUOTE && nStockID, NORET);
	CHECK_RET(m_pQuoteData, NORET);

	bool bIsSub = false;
	switch(nKLType)
	{
	case 1:
		bIsSub = m_pQuoteData->IsSubStockOneType(nStockID, StockSubType_KL_MIN1);
		break;

	case 2:
		bIsSub = m_pQuoteData->IsSubStockOneType(nStockID, StockSubType_KL_DAY);
		break;

	case 3:
		bIsSub = m_pQuoteData->IsSubStockOneType(nStockID, StockSubType_KL_WEEK);
		break;

	case 4:
		bIsSub = m_pQuoteData->IsSubStockOneType(nStockID, StockSubType_KL_MONTH);
		break;

	case 6:
		bIsSub = m_pQuoteData->IsSubStockOneType(nStockID, StockSubType_KL_MIN5);
		break;

	case 7:
		bIsSub = m_pQuoteData->IsSubStockOneType(nStockID, StockSubType_KL_MIN15);
		break;

	case 8:
		bIsSub = m_pQuoteData->IsSubStockOneType(nStockID, StockSubType_KL_MIN30);
		break;

	case 9:
		bIsSub = m_pQuoteData->IsSubStockOneType(nStockID, StockSubType_KL_MIN60);
		break;
	}

	if ( !bIsSub )
	{
		return;
	}

	bool bInReq = (m_mapReqInfo.find(std::make_pair(nStockID, nKLType)) != m_mapReqInfo.end());
	bool bInCache = (m_mapCacheData.find(std::make_pair(nStockID, nKLType)) != m_mapCacheData.end());

	////������ʱ��������ԭ����������������ݵ���ǰ��ǰ������
	if ( !bInReq && !bInCache )
	{
		//CHECK_OP(false, NOOP);
		return;
	}

	if ( (bInReq || bInCache) && m_pQuoteData->IsKLDataExist(nStockID, nKLType) )
	{
		//tomodify 3.1
		std::map<int, QuoteAckDataBody> &ackmap = m_mapCacheData[std::make_pair(nStockID, nKLType)];
		ackmap.clear();
		for ( int i = 0; i <= 2; i++)
		{
			Quote_StockKLData* pQuoteKL = NULL;
			int nCount = 0;
			if (m_pQuoteData->FillKLData(nStockID, pQuoteKL, nCount, nKLType, i))
			{
				QuoteAckDataBody &ack = ackmap[i];
				ack.nKLType = nKLType;
				ack.nRehabType = i;
				ack.vtKLData.clear();
				for ( int n = 0; n < nCount; n++ )
				{
					PushKLDataAckItem item;
					wchar_t szTime[64] = {}; 
					m_pQuoteData->TimeStampToStr(nStockID, pQuoteKL[n].dwTime,szTime);
					item.strTime = szTime;
					item.nOpenPrice = pQuoteKL[n].nOpenPrice;
					item.nClosePrice = pQuoteKL[n].nClosePrice;
					item.nHighestPrice = pQuoteKL[n].nHighestPrice;
					item.nLowestPrice = pQuoteKL[n].nLowestPrice;
					item.nPERatio= pQuoteKL[n].nPERatio;
					item.nTurnoverRate = pQuoteKL[n].nTurnoverRate;
					item.ddwTDVol= pQuoteKL[n].ddwTDVol;
					item.ddwTDVal = pQuoteKL[n].ddwTDVal;
					ack.vtKLData.push_back(item);
				}
			}
			m_pQuoteData->DeleteKLDataPointer(pQuoteKL);
		}
		m_pQuoteData->CheckRemoveQuoteKL(nStockID, nKLType);
		m_MsgHandler.RaiseEvent(EVENT_ID_ACK_REQUEST, 0, 0);
	}
}

void CPluginPushKLData::PushStockData(INT64 nStockID, SOCKET sock, StockSubType eStockSubType, DWORD dwTime)
{
	int nKLType = 0;
	switch(eStockSubType)
	{
	case StockSubType_KL_MIN1:
		{
			nKLType = 1;
		}
		break;
	case StockSubType_KL_DAY:
		{
			nKLType = 2;
		}
		break;
	case StockSubType_KL_WEEK:
		{
			nKLType = 3;
		}
		break;
	case StockSubType_KL_MONTH:
		{
			nKLType = 4;
		}
		break;
	case StockSubType_KL_MIN5:
		{
			nKLType = 6;
		}
		break;
	case StockSubType_KL_MIN15:
		{
			nKLType = 7;
		}
		break;
	case StockSubType_KL_MIN30:
		{
			nKLType = 8;
		}
		break;
	case StockSubType_KL_MIN60:
		{
			nKLType = 9;
		}
		break;
	default:
		CHECK_OP(false, NOOP);
		break;
	}

	if (m_pQuoteData->IsKLDataExist(nStockID, nKLType) )
	{
		Quote_StockKLData* pQuoteKL = NULL;
		int nCount = 0;
		if (m_pQuoteData->FillKLData(nStockID, pQuoteKL, nCount, nKLType, 0))
		{
			QuoteAckDataBody ackbody;
			ackbody.nKLType = nKLType;
			ackbody.nRehabType = 0;
			ackbody.vtKLData.clear();
			for ( int n = 0; n < nCount; n++ )
			{
				PushKLDataAckItem item;
				if ( pQuoteKL[n].dwTime <= dwTime )
				{
					continue;
				}
				wchar_t szTime[64] = {}; 
				m_pQuoteData->TimeStampToStr(nStockID, pQuoteKL[n].dwTime,szTime);
				item.strTime = szTime;
				item.nOpenPrice = pQuoteKL[n].nOpenPrice;
				item.nClosePrice = pQuoteKL[n].nClosePrice;
				item.nHighestPrice = pQuoteKL[n].nHighestPrice;
				item.nLowestPrice = pQuoteKL[n].nLowestPrice;
				item.nPERatio= pQuoteKL[n].nPERatio;
			    item.nTurnoverRate = pQuoteKL[n].nTurnoverRate;
				item.ddwTDVol= pQuoteKL[n].ddwTDVol;
				item.ddwTDVal = pQuoteKL[n].ddwTDVal;
				ackbody.vtKLData.push_back(item);
			}
			m_pQuoteData->DeleteKLDataPointer(pQuoteKL);
			
			if ( ackbody.vtKLData.size() > 50 )
			{
				std::reverse(ackbody.vtKLData.begin(), ackbody.vtKLData.end());
				ackbody.vtKLData.resize(1);
			}

			CProtoQuote::ProtoAckDataType ack;
			ack.head.nProtoID = PROTO_ID_PUSH_KLDATA;
			ack.head.ddwErrCode = 0;
			ack.head.nProtoVer = 1;
			ack.body = ackbody;

			StockMktType eMkt = StockMkt_HK;
			wchar_t szStockCode[16] = {};
			m_pQuoteData->GetStockInfoByHashVal(nStockID, eMkt, szStockCode);
			ack.body.nStockMarket = (int)eMkt;
			std::wstring wstrStockCode = szStockCode;
			ack.body.strStockCode.assign(wstrStockCode.begin(), wstrStockCode.end());

			if (ack.body.vtKLData.size() > 0)
			{
				CProtoQuote proto;
				proto.SetProtoData_Ack(&ack);

				Json::Value jsnAck;
				if (proto.MakeJson_Ack(jsnAck))
				{
					std::string strOut;
					CProtoParseBase::ConvJson2String(jsnAck, strOut, true);
					m_pQuoteServer->ReplyQuoteReq(PROTO_ID_PUSH_KLDATA, strOut.c_str(), (int)strOut.size(), sock);
				}
				else
				{
					CHECK_OP(false, NOOP);
				}
			}
		}
		m_pQuoteData->CheckRemoveQuoteKL(nStockID, nKLType);
	}
}

void CPluginPushKLData::OnTimeEvent(UINT nEventID)
{
	if ( TIMER_ID_CLEAR_CACHE == nEventID )
	{
		ClearQuoteDataCache();
	}
	else if ( TIMER_ID_HANDLE_TIMEOUT_REQ == nEventID )
	{
		HandleTimeoutReq();
	}
}

void CPluginPushKLData::OnMsgEvent(int nEvent,WPARAM wParam,LPARAM lParam)
{
	if ( EVENT_ID_ACK_REQUEST == nEvent )
	{
		ReplyAllReadyReq();
	}	
}

void CPluginPushKLData::ClearQuoteDataCache()
{
	if ( m_mapCacheToDel.empty() )
	{
		SetTimerClearCache(false);
		return ;
	}

	DWORD dwTickNow = ::GetTickCount();

	MAP_CACHE_TO_DESTROY::iterator it_todel = m_mapCacheToDel.begin();
	for ( ; it_todel != m_mapCacheToDel.end(); )
	{
		INT64 nStockID = it_todel->first.first;
		int nKLType = it_todel->first.second;
		DWORD dwToDelTick = it_todel->second;

		MAP_STOCK_DATA_REQ::iterator it_req = m_mapReqInfo.find(std::make_pair(nStockID, nKLType));
		if ( it_req != m_mapReqInfo.end() )
		{
			it_todel = m_mapCacheToDel.erase(it_todel);
		}
		else
		{
			if ( int(dwTickNow - dwToDelTick) > 60*1000 )
			{
				m_mapCacheData.erase(std::make_pair(nStockID, nKLType));
				it_todel = m_mapCacheToDel.erase(it_todel);

				StockMktCode stkMktCode;
				if ( m_pQuoteServer && GetStockMktCode(nStockID, stkMktCode) )
				{				
					//m_pQuoteServer->SubscribeQuote(stkMktCode.strCode, (StockMktType)stkMktCode.nMarketType, QUOTE_SERVER_TYPE, false, nKLType);					
				}
				else
				{
					CHECK_OP(false, NOOP);
				}
			}
			else
			{
				++it_todel;
			}			
		}
	}

	if ( m_mapCacheToDel.empty() )
	{
		SetTimerClearCache(false);		
	}
}

void CPluginPushKLData::HandleTimeoutReq()
{
	if ( m_mapReqInfo.empty() )
	{
		SetTimerHandleTimeout(false);
		return;
	}

	ReplyAllReadyReq();

	DWORD dwTickNow = ::GetTickCount();	
	MAP_STOCK_DATA_REQ::iterator it_stock = m_mapReqInfo.begin();
	for ( ; it_stock != m_mapReqInfo.end(); )
	{
		INT64 nStockID = it_stock->first.first;
		int nKLType = it_stock->first.second;
		VT_STOCK_DATA_REQ &vtReq = it_stock->second;
		VT_STOCK_DATA_REQ::iterator it_req = vtReq.begin();

		for ( ; it_req != vtReq.end(); )
		{
			StockDataReq *pReq = *it_req;
			if ( pReq == NULL )
			{
				CHECK_OP(false, NOOP);
				it_req = vtReq.erase(it_req);
				continue;
			}

			if ( int(dwTickNow - pReq->dwReqTick) > 5000 )
			{
				CStringA strTimeout;
				strTimeout.Format("KLData req timeout, market=%d, code=%s", pReq->req.body.nStockMarket, pReq->req.body.strStockCode.c_str());
				OutputDebugStringA(strTimeout.GetString());				
				ReplyDataReqError(pReq, PROTO_ERR_SERVER_TIMEROUT, L"����ʱ��");
				it_req = vtReq.erase(it_req);
				delete pReq;
			}
			else
			{
				++it_req;
			}
		}

		if ( vtReq.empty() )
		{
			//���ﲻ�������建��ʱ������Ϊ��ʱû�л��浱ǰ��Ʊ������
			it_stock = m_mapReqInfo.erase(it_stock);			
		}
		else
		{
			++it_stock;
		}
	}

	if ( m_mapReqInfo.empty() )
	{
		SetTimerHandleTimeout(false);
		return;
	}
}

void CPluginPushKLData::ReplyAllReadyReq()
{
	DWORD dwTickNow = ::GetTickCount();
	MAP_STOCK_DATA_REQ::iterator it_stock = m_mapReqInfo.begin();
	for ( ; it_stock != m_mapReqInfo.end(); )
	{
		INT64 nStockID = it_stock->first.first;
		int nKLType = it_stock->first.second;
		VT_STOCK_DATA_REQ &vtReq = it_stock->second;
		MAP_STOCK_CACHE_DATA::iterator it_data = m_mapCacheData.find(std::make_pair(nStockID, nKLType));

		if ( it_data == m_mapCacheData.end() )
		{
			++it_stock;
			continue;
		}

		VT_STOCK_DATA_REQ::iterator it_req = vtReq.begin();
		for ( ; it_req != vtReq.end(); ++it_req )
		{
			StockDataReq *pReq = *it_req;
			CHECK_OP(pReq, NOOP);

			std::map<int, QuoteAckDataBody> map = it_data->second;
			bool bFind = (map.find(pReq->req.body.nRehabType) != map.end());
			if (bFind)
			{
				ReplyStockDataReq(pReq, it_data->second[pReq->req.body.nRehabType]);
			}
			else
			{
				ReplyDataReqError(pReq, PROTO_ERR_UNKNOWN_ERROR, L"���ݶ�ȡʧ�ܣ�");
			}

			delete pReq;
		}

		vtReq.clear();

		it_stock = m_mapReqInfo.erase(it_stock);
		m_mapCacheToDel[std::make_pair(nStockID, nKLType)] = dwTickNow;
		SetTimerClearCache(true);
	}

	if ( m_mapReqInfo.empty() )
	{
		SetTimerHandleTimeout(false);
		return;
	}
}

void CPluginPushKLData::ReplyStockDataReq(StockDataReq *pReq, const QuoteAckDataBody &data)
{
	CHECK_RET(pReq && m_pQuoteServer, NORET);

	CProtoQuote::ProtoAckDataType ack;
	ack.head = pReq->req.head;
	ack.head.ddwErrCode = 0;
	ack.body = data;

	//tomodify 4
	ack.body.nStockMarket = pReq->req.body.nStockMarket;
	ack.body.strStockCode = pReq->req.body.strStockCode;
	ack.body.nKLType = pReq->req.body.nKLType;
	ack.body.nRehabType = pReq->req.body.nRehabType;
	
	if ( (int)ack.body.vtKLData.size() > pReq->req.body.nNum )
	{
		VT_KL_DATA_PUSH vtTemp;
		for (int nCount = (int)ack.body.vtKLData.size() - pReq->req.body.nNum; nCount < (int)ack.body.vtKLData.size(); nCount++)
		{
			vtTemp.push_back(ack.body.vtKLData[nCount]);
		}
		ack.body.vtKLData = vtTemp;
	}

	CProtoQuote proto;	
	proto.SetProtoData_Ack(&ack);

	Json::Value jsnAck;
	if ( proto.MakeJson_Ack(jsnAck) )
	{
		std::string strOut;
		CProtoParseBase::ConvJson2String(jsnAck, strOut, true);
		m_pQuoteServer->ReplyQuoteReq(pReq->req.head.nProtoID, strOut.c_str(), (int)strOut.size(), pReq->sock);
	}
	else
	{
		CHECK_OP(false, NOOP);
	}
}

void CPluginPushKLData::ReplyDataReqError(StockDataReq *pReq, int nErrCode, LPCWSTR pErrDesc)
{
	CHECK_RET(pReq && m_pQuoteServer, NORET);

	CProtoQuote::ProtoAckDataType ack;
	ack.head = pReq->req.head;
	ack.head.ddwErrCode = nErrCode;

	if ( pErrDesc )
	{
		CA::Unicode2UTF(pErrDesc, ack.head.strErrDesc);		 
	}

	CProtoQuote proto;	
	proto.SetProtoData_Ack(&ack);

	Json::Value jsnAck;
	if ( proto.MakeJson_Ack(jsnAck) )
	{
		std::string strOut;
		CProtoParseBase::ConvJson2String(jsnAck, strOut, true);
		m_pQuoteServer->ReplyQuoteReq(pReq->req.head.nProtoID, strOut.c_str(), (int)strOut.size(), pReq->sock);
	}
	else
	{
		CHECK_OP(false, NOOP);
	}

}

void CPluginPushKLData::SetTimerHandleTimeout(bool bStartOrStop)
{
	if ( m_bStartTimerHandleTimeout )
	{
		if ( !bStartOrStop )
		{			
			m_TimerWnd.StopTimer(TIMER_ID_HANDLE_TIMEOUT_REQ);
			m_bStartTimerHandleTimeout = FALSE;
		}
	}
	else
	{
		if ( bStartOrStop )
		{
			m_TimerWnd.StartMillionTimer(500, TIMER_ID_HANDLE_TIMEOUT_REQ);
			m_bStartTimerHandleTimeout = TRUE;
		}
	}
}

void CPluginPushKLData::SetTimerClearCache(bool bStartOrStop)
{
	if ( m_bStartTimerClearCache )
	{
		if ( !bStartOrStop )
		{
			m_TimerWnd.StopTimer(TIMER_ID_CLEAR_CACHE);
			m_bStartTimerClearCache = FALSE;
		}
	}
	else
	{
		if ( bStartOrStop )
		{
			m_TimerWnd.StartMillionTimer(50, TIMER_ID_CLEAR_CACHE);
			m_bStartTimerClearCache = TRUE;
		}
	}
}

bool CPluginPushKLData::GetStockMktCode(INT64 nStockID, StockMktCode &stkMktCode)
{
	MAP_STOCK_ID_CODE::iterator it_find = m_mapStockIDCode.find(nStockID);
	if ( it_find != m_mapStockIDCode.end())
	{
		stkMktCode = it_find->second;
		return true;
	}

	CHECK_OP(false, NOOP);
	return false;
}

void CPluginPushKLData::ClearAllReqCache()
{
	MAP_STOCK_DATA_REQ::iterator it_stock = m_mapReqInfo.begin();
	for ( ; it_stock != m_mapReqInfo.end(); ++it_stock )
	{
		VT_STOCK_DATA_REQ &vtReq = it_stock->second;
		VT_STOCK_DATA_REQ::iterator it_req = vtReq.begin();
		for ( ; it_req != vtReq.end(); ++it_req )
		{
			StockDataReq *pReq = *it_req;
			delete pReq;
		}
	}

	m_mapReqInfo.clear();
	m_mapCacheData.clear();
	m_mapCacheToDel.clear();
	m_mapStockIDCode.clear();
}

void CPluginPushKLData::SendAck(DWORD dwCookie, int nCSResult)
{
	MAP_STOCK_DATA_REQ::iterator it_stock = m_mapReqInfo.begin();
	for ( ; it_stock != m_mapReqInfo.end(); ++it_stock )
	{
		VT_STOCK_DATA_REQ &vtReq = it_stock->second;
		VT_STOCK_DATA_REQ::iterator it_req = vtReq.begin();
		for ( ; it_req != vtReq.end(); ++it_req )
		{
			StockDataReq* pReq = *it_req;
			INT64 nStockID = pReq->nStockID;
			if (pReq->dwLocalCookie == dwCookie)
			{
				if ( nCSResult != 0)
				{
					////���������ش���
				}
				else
				{	
					if ( m_pQuoteData->IsKLDataExist(nStockID, pReq->req.body.nKLType) )
					{
						//tomodify 3.1
						std::map<int, QuoteAckDataBody> &ackmap = m_mapCacheData[std::make_pair(nStockID, pReq->req.body.nKLType)];
						ackmap.clear();
						for ( int i = 0; i <= 2; i++)
						{
							Quote_StockKLData* pQuoteKL = NULL;
							int nCount = 0;
							if (m_pQuoteData->FillKLData(nStockID, pQuoteKL, nCount, pReq->req.body.nKLType, i))
							{
								QuoteAckDataBody &ack = ackmap[i];
								ack.nKLType = pReq->req.body.nKLType;
								ack.nRehabType = i;
								ack.nStockMarket = pReq->req.body.nStockMarket;
								ack.strStockCode = pReq->req.body.strStockCode;
								ack.vtKLData.clear();
								for ( int n = 0; n < nCount; n++ )
								{
									PushKLDataAckItem item;
									wchar_t szTime[64] = {}; 
									m_pQuoteData->TimeStampToStr(nStockID, pQuoteKL[n].dwTime,szTime);
									item.strTime = szTime;
									item.nOpenPrice = pQuoteKL[n].nOpenPrice;
									item.nClosePrice = pQuoteKL[n].nClosePrice;
									item.nHighestPrice = pQuoteKL[n].nHighestPrice;
									item.nLowestPrice = pQuoteKL[n].nLowestPrice;
									item.nPERatio= pQuoteKL[n].nPERatio;
									item.nTurnoverRate = pQuoteKL[n].nTurnoverRate;
									item.ddwTDVol= pQuoteKL[n].ddwTDVol;
									item.ddwTDVal = pQuoteKL[n].ddwTDVal;
									ack.vtKLData.push_back(item);
								}
							}
							m_pQuoteData->DeleteKLDataPointer(pQuoteKL);
						}
						m_pQuoteData->CheckRemoveQuoteKL(nStockID, pReq->req.body.nKLType);
						m_MsgHandler.RaiseEvent(EVENT_ID_ACK_REQUEST, 0, 0);
					}
				}
			}
		}
	}
}