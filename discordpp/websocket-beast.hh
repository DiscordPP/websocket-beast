//
// Created by Aidan on 6/20/2018.
//

#pragma once

#include <boost/beast.hpp>
#include <boost/beast/websocket/ssl.hpp>

namespace discordpp {
    template<class BASE>
    class WebsocketBeast : public BASE, virtual BotStruct {
        std::unique_ptr<boost::beast::websocket::stream<ssl::stream < tcp::socket>>>
        ws_;
        boost::beast::multi_buffer buffer_;

        // Report a failure
        void fail(boost::system::error_code ec, char const *what) {
            std::cerr << what << ": " << ec.message() << "\n";
        }

    public:
        void send(int opcode, json payload) override {
            ws_->write(boost::asio::buffer(json({{"op", opcode}, {"d", payload}}).dump()));
        };

    protected:
        void on_read(boost::system::error_code ec, std::size_t /*bytes_transferred*/) {
            if (ec) {
                return fail(ec, "read");
            }

            json jres;
            {
                std::ostringstream ss;

#if BOOST_VERSION >= 107000
                ss << boost::beast::make_printable(buffer_.data());
#else
                ss << boost::beast::buffers(buffer_.data());
#endif
				
                buffer_.consume(buffer_.size());
                //std::cerr << "Got " << ss.str() << '\n';
                jres = json::parse(ss.str());
            }

	        recievePayload(jres);

            ws_->async_read(
                    buffer_,
                    [this](boost::system::error_code ec, std::size_t bytes_transferred) {
                        on_read(ec, bytes_transferred);
                    }
            );
        }

        void runctd() override {
            json gateway = call("GET", "/gateway/bot");
            const std::string url = gateway["url"].get<std::string>().substr(6);
            /*
             *  + "/"
                                    + "?v=" + std::to_string(apiVersion)
                                    + "&encoding=json"*/

            // The SSL context is required, and holds certificates
            ssl::context ctx{ssl::context::tlsv12};

            // These objects perform our I/O
            tcp::resolver resolver{*aioc};
            ws_ = std::make_unique<boost::beast::websocket::stream<ssl::stream<tcp::socket>>>(*aioc, ctx);

            // Look up the domain name
            //std::cerr << gateway.dump(4) << url << std::endl;
            auto const results = resolver.resolve(url, "443");

            // Make the connection on the IP address we get from a lookup
            boost::asio::connect(ws_->next_layer().next_layer(), results.begin(), results.end());

            // Perform the SSL handshake
            ws_->next_layer().handshake(ssl::stream_base::client);

            // Perform the websocket handshake
            ws_->handshake(url, "/"
                                "?v=" + std::to_string(apiVersion) +
                                "&encoding=json"
            );

            ws_->async_read(
                    buffer_,
                    [this](boost::system::error_code ec, std::size_t bytes_transferred) {
                        on_read(ec, bytes_transferred);
                    }
            );

            BASE::runctd();
        }
    };
}
