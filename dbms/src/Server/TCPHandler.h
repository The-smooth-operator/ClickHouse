#pragma once

#include <DB/Core/Protocol.h>
#include <DB/Core/QueryProcessingStage.h>

#include <DB/IO/ReadHelpers.h>
#include <DB/IO/WriteHelpers.h>

#include <DB/DataStreams/BlockIO.h>

#include <statdaemons/Stopwatch.h>

#include "Server.h"


namespace DB
{


/// Состояние обработки запроса.
struct QueryState
{
	/// Идентификатор запроса.
	String query_id;

	QueryProcessingStage::Enum stage;
	Protocol::Compression::Enum compression;

	/// Откуда читать данные для INSERT-а.
	SharedPtr<ReadBuffer> maybe_compressed_in;
	BlockInputStreamPtr block_in;

	/// Куда писать возвращаемые данные.
	SharedPtr<WriteBuffer> maybe_compressed_out;
	BlockOutputStreamPtr block_out;

	/// Текст запроса.
	String query;
	/// Потоки блоков, с помощью которых выполнять запрос.
	BlockIO io;

	/// Отменен ли запрос
	bool is_cancelled;
	/// Пустой или нет
	bool is_empty;
	/// Данные были отправлены.
	bool sent_all_data;
	/// Запрос на вставку или нет.
	bool is_insert;

	/// Для вывода прогресса - разница после предыдущей отправки прогресса.
	volatile size_t rows_processed;
	volatile size_t bytes_processed;


	QueryState() : query_id(""), stage(QueryProcessingStage::Complete), compression(Protocol::Compression::Disable),
		is_cancelled(false), is_empty(true), sent_all_data(false), is_insert(false), rows_processed(0), bytes_processed(0) {}
	
	void reset()
	{
		*this = QueryState();
	}

	bool empty()
	{
		return is_empty;
	}
};


class TCPHandler : public Poco::Net::TCPServerConnection
{
public:
	TCPHandler(Server & server_, const Poco::Net::StreamSocket & socket_)
		: Poco::Net::TCPServerConnection(socket_), server(server_),
		log(&Logger::get("TCPHandler")), client_revision(0),
		connection_context(*server.global_context), query_context(connection_context)
	{
	}

	void run();

private:
	Server & server;
	Logger * log;

	UInt64 client_revision;

	Context connection_context;
	Context query_context;

	/// Потоки для чтения/записи из/в сокет соединения с клиентом.
	SharedPtr<ReadBuffer> in;
	SharedPtr<WriteBuffer> out;

	/// Время после последней проверки остановки запроса и отправки прогресса.
	Stopwatch after_check_cancelled;
	Stopwatch after_send_progress;

	String default_database;

	/// На данный момент, поддерживается одновременное выполнение только одного запроса в соединении.
	QueryState state;


	void runImpl();

	void receiveHello();
	bool receivePacket();
	void receiveQuery();
	bool receiveData();
	void readData(const Settings & global_settings);

	/// Обработать запрос INSERT
	void processInsertQuery(const Settings & global_settings);

	/// Обработать запрос, который не требует приёма блоков данных от клиента
	void processOrdinaryQuery();

	void sendHello();
	void sendData(Block & block);	/// Записать в сеть блок.
	void sendException(const Exception & e);
	void sendProgress();
	void sendEndOfStream();
	void sendProfileInfo();
	void sendTotals();
	void sendExtremes();

	/// Создаёт state.block_in/block_out для чтения/записи блоков, в зависимости от того, включено ли сжатие.
	void initBlockInput();
	void initBlockOutput();

	bool isQueryCancelled();

	/// Эта функция вызывается из разных потоков.
	void updateProgress(size_t rows, size_t bytes);

	/// Вывести информацию о скорости выполнения SELECT запроса.
	void logProfileInfo(Stopwatch & watch, IBlockInputStream & in);
};


}
