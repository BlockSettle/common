#ifndef MDHS_CLIENT_H
#define MDHS_CLIENT_H

#include <QObject>
#include <atomic>
#include <set>
#include <memory>
#include "market_data_history.pb.h"

class ApplicationSettings;
class ConnectionManager;
class RequestReplyCommand;

namespace spdlog
{
	class logger;
}

namespace Blocksettle
{
	namespace Communication
	{
		namespace MarketDataHistory
		{
			class MarketDataHistoryRequest;
		}
	}
}

using namespace Blocksettle::Communication::MarketDataHistory;

class MdhsClient : public QObject
{
	Q_OBJECT
public:
	MdhsClient(
		const std::shared_ptr<ApplicationSettings>& appSettings,
		const std::shared_ptr<ConnectionManager>& connectionManager,
		const std::shared_ptr<spdlog::logger>& logger,
		QObject* pParent = nullptr);

	~MdhsClient() noexcept;

	MdhsClient(const MdhsClient&) = delete;
	MdhsClient& operator = (const MdhsClient&) = delete;
	MdhsClient(MdhsClient&&) = delete;
	MdhsClient& operator = (MdhsClient&&) = delete;

	void SendRequest(const MarketDataHistoryRequest& request);

signals:
	void DataReceived(const std::string& data);

private:
	bool OnDataReceived(const std::string& data);

	std::shared_ptr<ApplicationSettings>	appSettings_;
	std::shared_ptr<ConnectionManager>		connectionManager_;
	std::shared_ptr<spdlog::logger>			logger_;

	std::atomic_flag                                lockCommands_ = ATOMIC_FLAG_INIT;
	std::set<std::shared_ptr<RequestReplyCommand>>  activeCommands_;
};

#endif // MDHS_CLIENT_H
