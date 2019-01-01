//
// Created by Aidan on 6/20/2018.
//

#ifndef DISCORDPP_WEBSOCKET_BEAST_HH
#define DISCORDPP_WEBSOCKET_BEAST_HH

#include <boost/beast.hpp>

namespace discordpp {
    template<class BASE>
    class WebsocketBeast : public BASE, virtual BotStruct {
        std::unique_ptr<boost::beast::websocket::stream<tcp::socket>> ws_;
        boost::beast::multi_buffer buffer_;

        // Report a failure
        void fail(boost::system::error_code ec, char const *what) {
            std::cerr << what << ": " << ec.message() << "\n";
        }

    public:
        virtual void send(int opcode, json payload) {
            ws_->write(boost::asio::buffer(json({{"op", opcode}, {"d",  payload}}).dump()));
        };

    protected:
        void runctd() override {
            json gateway = call("GET", "/gateway/bot");
            const std::string url = gateway["url"].get<std::string>() + "/"
                                    + "?v=" + std::to_string(apiVersion)
                                    + "&encoding=json";

            // These objects perform our I/O
            tcp::resolver resolver{*aioc};
            ws_ = std::make_unique<boost::beast::websocket::stream<tcp::socket>>(*aioc);

            // Look up the domain name
            auto const results = resolver.resolve(url);

            // Make the connection on the IP address we get from a lookup
            boost::asio::connect(ws_->next_layer(), results.begin(), results.end());

            // Perform the websocket handshake
            ws_->handshake(url, "/");

            ws_->async_read(
                    buffer_,
                    [this](boost::system::error_code ec, std::size_t /*bytes_transferred*/) {
                        if (ec) {
                            return fail(ec, "read");
                        }

                        json jres;
                        {
                            std::ostringstream ss;
                            ss << boost::beast::buffers(buffer_.data());
                            jres.parse(ss.str());
                        }
                    }
            );

            BASE::runctd();
        }
    };
}

#endif //DISCORDPP_WEBSOCKET_BEAST_HH
