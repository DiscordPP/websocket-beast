//
// Created by Aidan on 6/20/2018.
//

#ifndef DISCORDPP_WEBSOCKET_BEAST_HH
#define DISCORDPP_WEBSOCKET_BEAST_HH

#include <boost/beast.hpp>

namespace discordpp {
    template<class BASE>
    class WebsocketBeast : public BASE, virtual BotStruct{

    };
}

#endif //DISCORDPP_WEBSOCKET_BEAST_HH
