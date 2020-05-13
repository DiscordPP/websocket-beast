//
// Created by Aidan on 6/20/2018.
//

#pragma once

#include <boost/beast.hpp>
#include <boost/beast/websocket/ssl.hpp>

namespace discordpp{
	template<class BASE>
	class WebsocketBeast: public BASE, virtual BotStruct{
	public:
		virtual void
		initBot(unsigned int apiVersionIn, const std::string &tokenIn, std::shared_ptr<boost::asio::io_context> aiocIn) override{
			// The SSL context is required, and holds certificates
			ssl::context ctx{ssl::context::tlsv12};

			// These objects perform our I/O
			resolver_ = std::make_unique<tcp::resolver>(*aiocIn);
			ws_ = std::make_unique<boost::beast::websocket::stream<ssl::stream<tcp::socket>>>(*aiocIn, ctx);
		}

		virtual void send(const int opcode, sptr<const json> payload, sptr<const std::function<void()>> callback) override{
			ws_->write(boost::asio::buffer(json({
					{"op", opcode},
					{"d",  ((payload == nullptr)?json():*payload)}
			}).dump()));
			if(callback != nullptr){
				(*callback)();
			}
		};

	protected:
		void on_read(boost::system::error_code ec, std::size_t /*bytes_transferred*/){
			if(ec){
				return fail(ec, "read");
			}

			json jres;
			{
				std::ostringstream ss;

				ss << boost::beast::make_printable(buffer_.data());

				buffer_.consume(buffer_.size());
				jres = json::parse(ss.str());
			}

			receivePayload(jres);

			ws_->async_read(
					buffer_,
					[this](boost::system::error_code ec, std::size_t bytes_transferred){
						on_read(ec, bytes_transferred);
					}
			);
		}

		void runctd() override{
			connect();

			BASE::runctd();
		}

		virtual void connect(const std::function<void ()>& then = [](){}) override {
			call(
					std::make_shared<std::string>("GET"),
					std::make_shared<std::string>("/gateway/bot"),
					nullptr,
					std::make_shared<const std::function<void(const json)>>([this, &then](const json& gateway){
						std::cerr << gateway.dump(2) << std::endl;
						const std::string url = gateway["url"].get<std::string>().substr(6);

						// Look up the domain name
						auto const results = resolver_->resolve(url, "443");

						// Make the connection on the IP address we get from a lookup
						boost::asio::connect(ws_->next_layer().next_layer(), results.begin(), results.end());

						// Perform the SSL handshake
						ws_->next_layer().handshake(ssl::stream_base::client);

						// Perform the websocket handshake
						ws_->handshake(url, "/"
						                    "?v=" + std::to_string(apiVersion) +
						                    "&encoding=json"
						);

						then();

						ws_->async_read(
								buffer_,
								[this](boost::system::error_code ec, std::size_t bytes_transferred){
									on_read(ec, bytes_transferred);
								}
						);
					})
			);
		}

		private:

		// Report a failure
		void fail(boost::system::error_code ec, char const *what){
			std::cerr << what << ": " << ec.message() << "\n";
		}

		std::unique_ptr<boost::beast::websocket::stream<ssl::stream < tcp::socket>>> ws_;
		boost::beast::multi_buffer buffer_;
		std::unique_ptr<tcp::resolver> resolver_;
	};
}
