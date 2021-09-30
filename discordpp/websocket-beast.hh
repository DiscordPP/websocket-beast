//
// Created by Aidan on 6/20/2018.
//

#pragma once

#include <sstream>

#include <boost/beast.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <nlohmann/json.hpp>

#include <discordpp/botStruct.hh>
#include <discordpp/log.hh>

namespace discordpp {
using json = nlohmann::json;
namespace beast = boost::beast;   // from <boost/beast.hpp>
namespace ssl = boost::asio::ssl; // from <boost/asio/ssl.hpp>
using tcp = boost::asio::ip::tcp; // from <boost/asio/ip/tcp.hpp>

template <class BASE> class WebsocketBeast : public BASE, virtual BotStruct {
  public:
    virtual void
    initBot(unsigned int apiVersionIn, const std::string &tokenIn,
            std::shared_ptr<boost::asio::io_context> aiocIn) override {
        BASE::initBot(apiVersionIn, tokenIn, aiocIn);
        ctx_ = std::make_unique<ssl::context>(ssl::context::sslv23_client);
    }

    virtual void send(const int opcode, sptr<const json> payload,
                      sptr<const handleSent> callback) override {
        json out{{"op", opcode},
                 {"d", ((payload == nullptr) ? json() : *payload)}};

        log::log(log::debug, [out](std::ostream *log) {
            *log << "Sending: " << out.dump(4) << '\n';
        });

        ws_->write(boost::asio::buffer(out.dump()));
        if (callback != nullptr) {
            (*callback)();
        }
    };

  protected:
    void on_read(boost::system::error_code ec,
                 std::size_t /*bytes_transferred*/) {
        reading_ = false;
        if (ec) {
            if (!connected_ /* && ec == beast::websocket::error::closed*/) {
                connect();
                return;
            } else {
                return fail(ec, "read");
            }
        }

        json jres;
        {
            std::ostringstream ss;

            ss << beast::make_printable(buffer_->data());

            buffer_->consume(buffer_->size());
            jres = json::parse(ss.str());
        }

        receivePayload(jres);

        reading_ = true;
        ws_->async_read(*buffer_, [this](boost::system::error_code ec,
                                         std::size_t bytes_transferred) {
            on_read(ec, bytes_transferred);
        });
    }

    void runctd() override {
        connect();

        BASE::runctd();
    }

    virtual void connect() override {
        resolver_ = std::make_unique<tcp::resolver>(net::make_strand(*aioc));
        ws_ = std::make_unique<
            beast::websocket::stream<beast::ssl_stream<beast::tcp_stream>>>(
            net::make_strand(*aioc), *ctx_);
        buffer_ = std::make_unique<beast::multi_buffer>();

        call()
            ->method("GET")
            ->target("/gateway/bot")
            ->onRead([this](const bool error, const json &gateway) {
                if (error)
                    return;
                host_ = gateway["body"]["url"].get<std::string>().substr(6);
                // Look up the domain name
                resolver_->async_resolve(
                    host_, "443",
                    [this](beast::error_code ec,
                           tcp::resolver::results_type results) {
                        on_resolve(ec, results);
                    });
            })
            ->run();
    }

    virtual void disconnect() override {
        connected_ = false;

        ws_->async_close(beast::websocket::close_code::normal,
                         [this](beast::error_code ec) {
                             if (ec) {
                                 fail(ec, "close");
                             }

                             if (!reading_) {
                                 connect();
                             }

                             // WebSocket says that to close a connection you
                             // have to keep reading messages until you receive
                             // a close frame. Beast delivers the close frame as
                             // an error from read. However, Discord does not
                             // seem to send such a close frame.
                             /*beast::error_code dec;
                             beast::flat_buffer drain; // Throws everything away
                             efficiently do {
                                 // Keep reading messages...
                                 ws_->read(drain, dec);

                                 // ...until we get the special error code
                                 //if (dec == beast::websocket::error::closed)
                                 //    break;

                                 // Some other error occurred, report it and
                             exit.
                                 //if (ec)
                                 //    return fail(dec, "drain");
                             } while(!dec);

                             std::cerr << "Sleeping" << std::flush;
                             for (int i = 0; i < 3; i++) {
                                 boost::asio::deadline_timer t(*aioc,
                             boost::posix_time::seconds(1)); t.wait(); std::cerr
                             << '.' << std::flush;
                             }
                             std::cerr << " Ok enough of that." << std::endl;

                             (*after)();*/
                         });
    }

  private:
    void on_resolve(beast::error_code ec, tcp::resolver::results_type results) {
        if (ec)
            return fail(ec, "resolve");

        // Set a timeout on the operation
        beast::get_lowest_layer(*ws_).expires_after(std::chrono::seconds(30));

        // Make the connection on the IP address we get from a lookup
        beast::get_lowest_layer(*ws_).async_connect(
            results, [this](beast::error_code ec,
                            tcp::resolver::results_type::endpoint_type ep) {
                on_connect(ec, ep);
            });
    }

    void on_connect(beast::error_code ec,
                    tcp::resolver::results_type::endpoint_type ep) {
        if (ec)
            return fail(ec, "connect");

        // Update the host_ string. This will provide the value of the
        // Host HTTP header during the WebSocket handshake.
        // See https://tools.ietf.org/html/rfc7230#section-5.4
        host_ += ':' + std::to_string(ep.port());

        // Set a timeout on the operation
        beast::get_lowest_layer(*ws_).expires_after(std::chrono::seconds(30));

        // Perform the SSL handshake
        ws_->next_layer().async_handshake(
            ssl::stream_base::client,
            beast::bind_front_handler(
                [this](beast::error_code ec) { on_ssl_handshake(ec); }));
    }

    void on_ssl_handshake(beast::error_code ec) {
        if (ec)
            return fail(ec, "ssl_handshake");

        // Turn off the timeout on the tcp_stream, because
        // the websocket stream has its own timeout system.
        beast::get_lowest_layer(*ws_).expires_never();

        // Set suggested timeout settings for the websocket
        ws_->set_option(beast::websocket::stream_base::timeout::suggested(
            beast::role_type::client));

        // Set a decorator to change the User-Agent of the handshake
        ws_->set_option(beast::websocket::stream_base::decorator(
            [](beast::websocket::request_type &req) {
                req.set(http::field::user_agent,
                        std::string(BOOST_BEAST_VERSION_STRING) +
                            " websocket-client-async-ssl");
            }));

        // Perform the websocket handshake
        ws_->async_handshake(
            host_,
            "/"
            "?v=" +
                std::to_string(apiVersion) + "&encoding=json",
            [this](beast::error_code ec) { on_handshake(ec); });
    }

    void on_handshake(beast::error_code ec) {
        if (ec)
            return fail(ec, "handshake");

        connected_ = true;

        // Start listening
        reading_ = true;
        ws_->async_read(*buffer_, [this](boost::system::error_code ec,
                                         std::size_t bytes_transferred) {
            on_read(ec, bytes_transferred);
        });
    }

    // Report a failure
    void fail(boost::system::error_code ec, char const *what) {
        std::ostringstream error;
        if (!connected_) {
            std::cerr << "Beast Websocket failure: " << what << ": "
                      << ec.message() << "\n But that was expected.\n";
            return;
        }
        std::cerr << "Beast Websocket failure: " << what << ": " << ec.message()
                  << "\n";
        reconnect(error.str());
    }

    std::unique_ptr<tcp::resolver> resolver_;
    std::unique_ptr<
        beast::websocket::stream<beast::ssl_stream<beast::tcp_stream>>>
        ws_;
    std::unique_ptr<beast::multi_buffer> buffer_;
    std::string host_;
    std::unique_ptr<ssl::context> ctx_;
    bool connected_ = false;
    bool reading_ = false;
};
} // namespace discordpp
