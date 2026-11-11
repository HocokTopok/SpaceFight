#include "gd/ai.hpp"
#include "gd/network.hpp"
#include <SFML/Graphics.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <random>
#include <functional>
#include <cstdlib>

namespace
{
using namespace gd;
const sf::Color ink(8,14,27), panel(13,24,42,245), border(40,62,84), muted(137,159,179), white(224,236,242), cyan(97,226,215), orange(250,152,98);
const std::array<sf::Color,5> planetColors{{{81,155,232},{222,161,102},{165,220,243},{240,104,65},{192,168,223}}};
sf::Color ownerColor(int owner) { return owner == 0 ? cyan : owner == 1 ? orange : sf::Color(109,128,152); }
struct Button { sf::FloatRect rect; std::function<void()> action; bool enabled; };
struct Effect { int planet; float age = 0; };
class VisualEvents : public EventObserver
{
public :
    void on_event(const Event & e) override { if (e.battle && e.planet >= 0) effects.push_back({e.planet}); }
    std::vector<Effect> effects;
};
struct Star { float x,y,size,phase; };
class App
{
public:
    App(const std::filesystem::path & assets, bool smoke, const std::string & screenshot)
        : m_window(sf::VideoMode({1440,900}), "Galactic Dominion", sf::Style::Default), m_smoke(smoke), m_screenshot(screenshot)
    {
        m_window.setFramerateLimit(60);
        if (!m_font.openFromFile(assets/"Tuffy.ttf")) throw std::runtime_error("Cannot load font from " + assets.string());
        if (!m_background.loadFromFile(assets/"nebula.png")) throw std::runtime_error("Cannot load nebula texture");
        m_background.setSmooth(true);
        for (int i=0;i<5;++i) { if (!m_planets[i].loadFromFile(assets/("planet_"+std::to_string(i)+".png"))) throw std::runtime_error("Cannot load planet texture"); m_planets[i].setSmooth(true); }
        std::mt19937 rng(71);
        for(int i=0;i<420;++i) m_stars.push_back({float(rng()%1440),float(rng()%900),.4f+float(rng()%15)/10,float(rng()%628)/100});
        const char * base = std::getenv("HOME");
        m_save = std::filesystem::path(base ? base : ".") / ".galactic-dominion" / "campaign.save";
        if (m_smoke) start();
    }
    void run()
    {
        sf::Clock delta;
        int frames=0;
        while(m_window.isOpen())
        {
            float dt=std::min(.1f,delta.restart().asSeconds()); m_time+=dt;
            while(auto e=m_window.pollEvent())
            {
                if(e->is<sf::Event::Closed>()) m_window.close();
                if(auto resized=e->getIf<sf::Event::Resized>()) resize(resized->size);
                if(auto text=e->getIf<sf::Event::TextEntered>(); text && m_menu && m_ipFocus)
                {
                    auto c=text->unicode;
                    if(c==8 && !m_ip.empty()) m_ip.pop_back();
                    else if((c=='.'||(c>='0'&&c<='9'))&&m_ip.size()<15) m_ip+=static_cast<char>(c);
                }
                if(auto k=e->getIf<sf::Event::KeyPressed>()) key(k->code);
                if(auto click=e->getIf<sf::Event::MouseButtonPressed>(); click && click->button==sf::Mouse::Button::Left)
                {
                    auto pos=m_window.mapPixelToCoords(click->position);
                    bool used=false;
                    for(auto it=m_buttons.rbegin();it!=m_buttons.rend();++it) if(it->rect.contains(pos))
                    { auto action=it->action; bool enabled=it->enabled; used=true; if(enabled) action(); break; }
                    if(!used && !m_menu && !m_research && !m_help && m_game->world().winner==-1) select(pos);
                }
            }
            if(m_network) m_network->update();
            if(m_effects) { for(auto & e:m_effects->effects) e.age+=dt; std::erase_if(m_effects->effects,[](auto e){return e.age>2.4f;}); }
            if(!m_menu && m_game && m_game->world().turn!=m_seenTurn)
            {
                m_seenTurn=m_game->world().turn;
                if(!m_network && !m_smoke) save();
            }
            m_buttons.clear(); m_window.clear(ink); backdrop();
            if(m_menu) menu(); else game();
            if(m_help) help();
            m_window.display();
            if(m_smoke && ++frames==10)
            {
                sf::Texture capture(m_window.getSize()); capture.update(m_window);
                if(!capture.copyToImage().saveToFile(m_screenshot)) throw std::runtime_error("Screenshot failed");
                m_window.close();
            }
        }
    }
private:
    void resize(sf::Vector2u size)
    {
        if(size.x==0||size.y==0) return;
        sf::View view(sf::FloatRect({0,0},{1440,900})); float ratio=float(size.x)/size.y, target=1.6f;
        if(ratio>target) view.setViewport({{(1-target/ratio)/2,0},{target/ratio,1}});
        else view.setViewport({{0,(1-ratio/target)/2},{1,ratio/target}});
        m_window.setView(view);
    }
    void text(std::string value,float x,float y,unsigned size=16,sf::Color color=white)
    {
        sf::Text t(m_font,value,size); t.setPosition({x,y}); t.setFillColor(color); m_window.draw(t);
    }
    void rect(float x,float y,float w,float h,sf::Color fill,sf::Color outline=sf::Color::Transparent)
    {
        sf::RectangleShape r({w,h});r.setPosition({x,y});r.setFillColor(fill);r.setOutlineThickness(1);r.setOutlineColor(outline);m_window.draw(r);
    }
    void circle(sf::Vector2f p,float radius,sf::Color color,float outline=0,sf::Color stroke=sf::Color::Transparent)
    {
        sf::CircleShape c(radius,64);c.setOrigin({radius,radius});c.setPosition(p);c.setFillColor(color);c.setOutlineThickness(outline);c.setOutlineColor(stroke);m_window.draw(c);
    }
    void line(sf::Vector2f a,sf::Vector2f b,sf::Color color,float width=1)
    {
        auto d=b-a;float length=std::hypot(d.x,d.y);sf::RectangleShape r({length,width});r.setPosition(a);r.setRotation(sf::radians(std::atan2(d.y,d.x)));r.setFillColor(color);m_window.draw(r);
    }
    void button(std::string label,float x,float y,float w,float h,std::function<void()> action,bool enabled=true,bool accent=false)
    {
        sf::FloatRect bounds({x,y},{w,h});bool hover=bounds.contains(m_window.mapPixelToCoords(sf::Mouse::getPosition(m_window)));
        sf::Color fill=accent?sf::Color(34,101,104):sf::Color(23,39,57);
        if(hover&&enabled) fill=accent?sf::Color(45,132,130):sf::Color(40,63,81);
        if(!enabled) fill=sf::Color(17,27,41);
        rect(x,y,w,h,fill,accent&&enabled?cyan:border); text(label,x+12,y+(h-22)/2,16,enabled?white:muted);
        m_buttons.push_back({bounds,std::move(action),enabled});
    }
    void backdrop()
    {
        sf::Sprite bg(m_background);bg.setScale({1440.f/m_background.getSize().x,900.f/m_background.getSize().y});m_window.draw(bg);
        for(auto s:m_stars)
        {
            float x=std::fmod(s.x+m_time*(.4f+s.size*.3f),1440.f);
            auto alpha=static_cast<std::uint8_t>(90+100*(.5+.5*std::sin(m_time*.8+s.phase)));
            circle({x,s.y},s.size,sf::Color(192,213,239,alpha));
        }
    }
    void planetSprite(PlanetType type,sf::Vector2f pos,float radius,float angle=0)
    {
        sf::Sprite sprite(m_planets[static_cast<int>(type)]);sprite.setOrigin({128,128});sprite.setPosition(pos);sprite.setScale({radius/111.f,radius/111.f});sprite.setRotation(sf::degrees(angle));m_window.draw(sprite);
    }
    void start()
    {
        m_network.reset(); m_game=std::make_unique<GameFacade>(m_seed++); m_effects=std::make_shared<VisualEvents>();m_game->events().subscribe(m_effects);
        m_menu=false;m_research=false;m_help=false;m_selected=0;m_player=0;m_seenTurn=1;m_moving=false;m_status="Select a colony. Queue orders, then commit the turn.";
    }
    void save()
    {
        try { std::filesystem::create_directories(m_save.parent_path());saveWorld(m_game->world(),m_save.string());m_status="Saved: completed turn "+std::to_string(m_game->world().turn-1); }
        catch(const std::exception & e){m_status=e.what();}
    }
    void load()
    {
        try { auto w=loadWorld(m_save.string());start();m_game->replace(std::move(w));m_seenTurn=m_game->world().turn;m_status="Campaign restored"; }
        catch(const std::exception & e){m_status=e.what();}
    }
    void lan(bool host)
    {
        try
        {
            start();m_network=std::make_unique<NetworkSession>(*m_game);
            if(host)m_network->host();else m_network->join(m_ip);
            m_player=host?0:1;m_selected=host?0:19;
        }
        catch(const std::exception & e){m_status=e.what();m_network.reset();m_menu=true;}
    }
    void menu()
    {
        planetSprite(PlanetType::Terran,{1110,400},260,-7);
        circle({1110,400},286,sf::Color::Transparent,1,sf::Color(78,136,159,90));
        text("A TURN-BASED SPACE STRATEGY",92,92,16,cyan);
        text("GALACTIC",86,133,72);text("DOMINION",86,208,72);
        text("Build an empire. Read your rival. Command the stars.",92,312,21,muted);
        text("OPPONENT DOCTRINE",94,377,13,cyan);
        for(int i=0;i<4;++i)button(makeAI(i)->name(),94+i*148,405,138,38,[this,i]{m_aiStyle=i;},true,m_aiStyle==i);
        button("NEW CAMPAIGN",94,467,284,52,[this]{start();},true,true);
        button("CONTINUE SAVE",394,467,284,52,[this]{load();},std::filesystem::exists(m_save));
        button("HOST LAN / :4242",94,541,284,48,[this]{lan(true);});
        button("JOIN LAN",394,541,284,48,[this]{lan(false);});
        text("HOST IPv4",96,612,14,muted);
        button(m_ip+(m_ipFocus?" |":""),220,601,458,42,[this]{m_ipFocus=true;});
        button("HOW TO PLAY",94,675,284,44,[this]{m_help=true;});
        button("QUIT",394,675,284,44,[this]{m_window.close();});
        text(m_status,94,748,15,orange);
        text("01 / AURORA  vs  02 / EMBER",95,833,15,muted);
        text("C++23  /  SFML 3  /  SIMULTANEOUS TURNS",900,844,13,muted);
    }
    sf::Vector2f position(const Planet & p) const { return {58+p.x*1.14f,121+p.y*.87f}; }
    bool canAct() const { return m_game->world().winner==-1&&!m_game->ready(m_player)&&(!m_network||m_network->connected()); }
    void queue(Order o)
    {
        if(!canAct())return;
        auto error=m_game->queue(m_player,o);m_status=error.empty()?"Order queued. Resources reserved until commit.":error;
    }
    void select(sf::Vector2f point)
    {
        const auto & w=m_game->world();
        for(const auto & p:w.planets)if(std::hypot(position(p).x-point.x,position(p).y-point.y)<p.radius+16)
        {
            if(m_moving)
            {
                auto preview=m_game->preview(m_player);auto ships=preview.planets[m_selected].ships;
                for(auto & n:ships)n=(n*m_fraction+99)/100;
                queue({OrderKind::Move,m_selected,p.id,0,ships});m_moving=false;
            }
            else m_selected=p.id;
            return;
        }
    }
    void endTurn()
    {
        if(!canAct())return;
        if(m_network){m_network->submit();return;}
        auto orders=makeAI(m_aiStyle)->plan(m_game->world(),1);
        for(auto o:orders)m_game->queue(1,o);
        m_game->commit(0);m_game->commit(1);m_game->resolve();m_moving=false;
    }
    void key(sf::Keyboard::Key code)
    {
        if(code==sf::Keyboard::Key::Escape)
        {
            if(m_help)m_help=false;else if(m_research)m_research=false;else if(m_moving)m_moving=false;
            else if(!m_menu){if(!m_network&&!m_smoke)save();m_menu=true;m_network.reset();}
            return;
        }
        if(m_menu)return;
        if(code==sf::Keyboard::Key::R)m_research=!m_research;
        if(code==sf::Keyboard::Key::Enter&&!m_research&&!m_help)endTurn();
        if(!m_network)
        {
            if(code==sf::Keyboard::Key::F5)save();
            if(code==sf::Keyboard::Key::F9)load();
            if(code==sf::Keyboard::Key::U){if(m_game->undo())m_status="Turn undone";}
        }
    }
    void galaxy(const World & view)
    {
        for(auto lane:view.lanes)
        {
            auto a=position(view.planets[lane[0]]),b=position(view.planets[lane[1]]);
            bool highlighted=lane[0]==m_selected||lane[1]==m_selected;
            line(a,b,highlighted?sf::Color(85,164,175,135):sf::Color(51,77,101,105),highlighted?1.5f:1.f);
            if(highlighted){auto point=a+(b-a)*(.5f+.35f*std::sin(m_time*.4f));circle(point,2,cyan);}
        }
        for(const auto & p:view.planets)
        {
            auto pos=position(p);auto color=ownerColor(p.owner);
            if(p.id==m_selected)
            {
                circle(pos,p.radius+15+std::sin(m_time*2)*2,sf::Color::Transparent,1.5f,cyan);
                line(pos+sf::Vector2f{-p.radius-22,0},pos+sf::Vector2f{-p.radius-14,0},cyan,2);
                line(pos+sf::Vector2f{p.radius+14,0},pos+sf::Vector2f{p.radius+22,0},cyan,2);
            }
            if(p.type==PlanetType::Gas)
            {
                sf::CircleShape ring(p.radius+13,80);ring.setOrigin({p.radius+13,p.radius+13});ring.setPosition(pos);ring.setScale({1.35f,.35f});ring.setRotation(sf::degrees(-24));ring.setFillColor(sf::Color::Transparent);ring.setOutlineColor(sf::Color(191,169,153,140));ring.setOutlineThickness(4);m_window.draw(ring);
            }
            planetSprite(p.type,pos,p.radius,std::sin(m_time*.05f+p.id)*4);
            circle(pos,p.radius+5,sf::Color::Transparent,1,sf::Color(color.r,color.g,color.b,95));
            if(p.owner>=0)
            {
                int satellites=p.buildings[3]+p.buildings[4]+p.buildings[5];
                for(int i=0;i<satellites;++i)
                {
                    float phase=m_time*.18f+i*2.1f+p.id;sf::Vector2f v{std::cos(phase)*(p.radius+12),std::sin(phase)*(p.radius+12)*.6f};
                    rect(pos.x+v.x-3,pos.y+v.y-2,6,4,color);line(pos+v+sf::Vector2f{-7,0},pos+v+sf::Vector2f{7,0},muted);
                }
                for(int i=0;i<p.buildings[0]+p.buildings[1]+p.buildings[2];++i)
                    circle(pos+sf::Vector2f{std::sin(i*2.8f+p.id)*p.radius*.55f,std::cos(i*1.8f)*p.radius*.5f},1.4f,color);
            }
            circle(pos+sf::Vector2f{std::cos(m_time*.08f+p.id)*(p.radius+21),std::sin(m_time*.08f+p.id)*(p.radius+21)},2.3f,muted);
            text(p.name+(p.capital>=0?"  *":""),pos.x-30,pos.y+p.radius+11,15,p.owner>=0?color:white);
            bool seen=visible(view,m_player,p.id);
            text(seen?std::to_string(shipCount(p.ships))+" ships":"unscanned",pos.x-27,pos.y+p.radius+31,12,muted);
            if(!p.jobs.empty()){rect(pos.x-19,pos.y+p.radius+49,38,2,border);rect(pos.x-19,pos.y+p.radius+49,38.f/(p.jobs.front().remaining+1),2,color);}
        }
        for(const auto & f:view.transits)
        {
            if(f.owner!=m_player&&!visible(view,m_player,f.from)&&!visible(view,m_player,f.to))continue;
            auto a=position(view.planets[f.from]),b=position(view.planets[f.to]);
            float progress=std::clamp(float(f.duration-f.remaining+.3)/f.duration,.12f,.88f);
            auto p=a+(b-a)*progress;auto color=ownerColor(f.owner);
            line(a,p,sf::Color(color.r,color.g,color.b,80),2);
            sf::ConvexShape ship(3);ship.setPoint(0,{8,0});ship.setPoint(1,{-5,-4});ship.setPoint(2,{-5,4});ship.setPosition(p);ship.setRotation(sf::radians(std::atan2(b.y-a.y,b.x-a.x)));ship.setFillColor(color);m_window.draw(ship);
            text(std::to_string(shipCount(f.ships))+" / "+std::to_string(f.remaining)+"t",p.x+8,p.y-17,11,color);
            circle(p-sf::Vector2f{5,0},2+std::sin(m_time*9),sf::Color(color.r,color.g,color.b,120));
        }
        for(auto e:m_effects->effects)
        {
            auto pos=position(view.planets[e.planet]);auto alpha=static_cast<std::uint8_t>(std::max(0.f,1-e.age/2.4f)*200);
            circle(pos,20+e.age*48,sf::Color::Transparent,2,sf::Color(255,175,98,alpha));
            for(int i=0;i<10;++i){float angle=i*.628f;auto a=pos+sf::Vector2f{std::cos(angle),std::sin(angle)}*(10+e.age*25);line(a,a+sf::Vector2f{std::cos(angle),std::sin(angle)}*12.f,sf::Color(255,225,180,alpha),2);}
        }
    }
    void sidebar(const World & view)
    {
        const auto & p=view.planets[m_selected];const auto & empire=view.players[m_player];
        rect(1080,103,334,710,panel,border);
        text("PLANET / "+std::to_string(p.id+1),1102,121,12,muted);
        text(p.name,1102,146,29);text(planetTypeName(p.type),1103,187,15,planetColors[static_cast<int>(p.type)]);
        text(p.owner==m_player?"YOUR COLONY":p.owner==-1?"NEUTRAL WORLD":"ENEMY COLONY",1103,216,13,ownerColor(p.owner));
        bool seen=visible(view,m_player,p.id);
        if(seen)
        {
            text("Defense "+std::to_string(p.defense)+"    Ships "+std::to_string(shipCount(p.ships)),1103,243,16);
            auto rate=income(p,p.owner>=0?view.players[p.owner]:empire);
            text("Yield  M +"+std::to_string(rate.metal)+"  E "+(rate.energy>=0?"+":"")+std::to_string(rate.energy)+"  S +"+std::to_string(rate.science),1103,272,14,muted);
        }
        else text("Out of sensor range",1103,249,16,muted);
        for(int i=0;i<3;++i)button(std::array<std::string,3>{"BUILD","FLEET","DETAILS"}[i],1102+i*99,312,93,34,[this,i]{m_tab=i;},true,m_tab==i);
        bool own=p.owner==m_player;int id=p.id;
        if(m_tab==0)
        {
            int used=0;for(auto b:p.buildings)used+=b;for(auto j:p.jobs)if(!j.ship)++used;
            text("SLOTS "+std::to_string(used)+" / "+std::to_string(p.slots)+"  (includes queued)",1103,362,13,muted);
            for(int b=0;b<6;++b)
            {
                const auto & spec=buildingSpecs()[b];auto cost=price(spec.cost,empire);float y=392+b*57;
                auto copy=view;bool valid=applyOrder(copy,m_player,{OrderKind::Build,id,0,b}).empty();
                button(spec.name+"  ["+std::to_string(p.buildings[b])+"]",1103,y,288,34,[this,id,b]{queue({OrderKind::Build,id,0,b});},own&&canAct()&&valid);
                text("M "+std::to_string(cost.metal)+"  E "+std::to_string(cost.energy)+"   /   "+std::to_string(std::max(1,spec.turns-int(empire.tech[1])))+" turns",1113,y+35,12,muted);
            }
        }
        else if(m_tab==1)
        {
            text("ORBITAL FLEET / "+std::string(p.buildings[3]?"SHIPYARD":"NO SHIPYARD"),1103,362,13,muted);
            for(int t=0;t<4;++t)
            {
                auto spec=shipSpecs()[t];auto cost=price(spec.cost,empire);float y=392+t*66;
                text(spec.name+"  x"+(seen?std::to_string(p.ships[t]):"?"),1103,y,17);
                text("ATK "+std::to_string(spec.attack)+" / HP "+std::to_string(spec.armor),1103,y+24,12,muted);
                auto copy=view;bool valid=applyOrder(copy,m_player,{OrderKind::Produce,id,0,t}).empty();
                button("+ BUILD",1283,y,108,31,[this,id,t]{queue({OrderKind::Produce,id,0,t});},own&&canAct()&&valid);
                text("M "+std::to_string(cost.metal)+" E "+std::to_string(cost.energy),1283,y+34,11,muted);
            }
            text("Cruiser / bomber require Heavy ships",1103,659,12,muted);
            button("SEND "+std::to_string(m_fraction)+"%",1103,691,179,42,[this]{m_moving=true;m_status="Click a neighboring planet to send the selected fleet.";},own&&canAct()&&shipCount(p.ships)>0,true);
            button("%",1293,691,98,42,[this]{m_fraction=m_fraction==100?25:m_fraction+25;},own);
            text("Choose a connected destination on the map.",1103,747,12,muted);
        }
        else
        {
            if(!seen){text("Build Radar or research Deep sensors.",1103,380,13,muted);return;}
            text("PRODUCTION QUEUE",1103,368,13,cyan);
            if(p.jobs.empty())text("No active projects",1103,400,15,muted);
            int row=0;
            for(auto j:p.jobs)
            {
                auto name=j.ship?shipSpecs()[j.type].name:buildingSpecs()[j.type].name;
                text(name+" / "+std::to_string(j.remaining)+"t",1103,399+row*23,14);++row;
            }
            text("BUILDINGS",1103,697,13,cyan);
            std::string buildings;
            for(int b=0;b<6;++b)if(p.buildings[b])buildings+=buildingSpecs()[b].name+" ";
            text(buildings.substr(0,35),1103,726,13,muted);text(buildings.size()>35?buildings.substr(35,35):"",1103,748,13,muted);
        }
    }
    void game()
    {
        auto view=m_game->preview(m_player);const auto & player=view.players[m_player];
        galaxy(view);sidebar(view);
        rect(0,0,1440,82,sf::Color(9,18,31,250));line({0,82},{1440,82},border);
        text("GALACTIC DOMINION",28,18,21);text(m_player==0?"AURORA COMMAND":"EMBER COMMAND",29,48,12,ownerColor(m_player));
        text("METAL",347,16,12,muted);text(std::to_string(player.resources.metal),347,36,24);
        text("ENERGY",491,16,12,muted);text(std::to_string(player.resources.energy)+(player.shortage?" !":""),491,36,24,player.shortage?orange:white);
        text("SCIENCE",642,16,12,muted);text(std::to_string(player.resources.science),642,36,24);
        button("RESEARCH [R]",794,22,161,39,[this]{m_research=true;});
        text("TURN",986,16,12,muted);text(std::to_string(view.turn),990,36,24);
        button(m_game->ready(m_player)?"WAITING...":"COMMIT TURN",1096,19,205,45,[this]{endTurn();},canAct(),true);
        button("MENU",1312,19,101,45,[this]{if(!m_network&&!m_smoke)save();m_network.reset();m_menu=true;});
        text("SECTOR 07 / THE PERSEUS REACH",28,99,12,muted);
        text("* CAPITAL   /   "+std::string(m_network?"LAN PvP":"AI: "+makeAI(m_aiStyle)->name()),698,99,12,muted);
        rect(24,767,1035,108,sf::Color(11,21,36,240),border);
        text(m_moving?"SELECT DESTINATION":"COMMAND LOG",43,780,12,m_moving?cyan:muted);
        auto & entries=m_game->log()->entries;
        int count=std::min(3,int(entries.size()));
        for(int i=0;i<count;++i)text(entries[entries.size()-count+i].text.substr(0,95),43,802+i*20,13,i==count-1?white:muted);
        if(entries.empty())text("Expand along hyperlanes. Capture the enemy capital to win.",43,808,15,muted);
        text("ORDERS "+std::to_string(m_game->orders(m_player).size()),1103,830,13,cyan);
        button("CLEAR",1230,821,162,34,[this]{m_game->clear(m_player);m_status="Pending orders cleared";},canAct());
        text((m_network?m_network->status():m_status).substr(0,132),28,880,12,m_network&&m_network->failed()?orange:muted);
        if(m_research)research(view);
        if(view.winner!=-1)
        {
            m_buttons.clear();rect(0,82,1440,818,sf::Color(3,9,20,205));rect(430,285,580,300,panel,border);
            text(view.winner==2?"STALEMATE":view.winner==m_player?"GALAXY SECURED":"EMPIRE FALLEN",470,326,36,view.winner==m_player?cyan:orange);
            text("Campaign ended on turn "+std::to_string(view.turn-1),475,391,20,muted);
            button("MAIN MENU",475,465,215,48,[this]{m_network.reset();m_menu=true;},true,true);
            button("NEW CAMPAIGN",713,465,250,48,[this]{start();});
        }
    }
    void research(const World & view)
    {
        m_buttons.clear();rect(0,82,1440,818,sf::Color(3,8,18,222));rect(142,148,1156,628,panel,border);
        text("RESEARCH DIRECTORATE",176,174,28);text("Choose a doctrine. Science is invested after each turn.",177,218,16,muted);
        button("CLOSE",1158,175,106,37,[this]{m_research=false;});
        const auto & player=view.players[m_player];
        for(int branch=0;branch<3;++branch)
        {
            float x=178+branch*368;text(std::array<std::string,3>{"01 / INDUSTRY","02 / WARFARE","03 / EXPLORATION"}[branch],x,271,14,cyan);
            for(int depth=0;depth<3;++depth)
            {
                int t=branch*3+depth;const auto & spec=techSpecs()[t];float y=306+depth*142;
                rect(x,y,340,124,sf::Color(20,34,52),player.tech[t]?cyan:border);
                text(spec.name,x+14,y+10,21);text(spec.description,x+14,y+42,13,muted);
                std::string label=player.tech[t]?"COMPLETE":player.research==t?"IN PROGRESS "+std::to_string(player.progress)+" / "+std::to_string(spec.cost):"RESEARCH / "+std::to_string(spec.cost)+" S";
                auto copy=view;bool valid=applyOrder(copy,m_player,{OrderKind::Research,0,0,t}).empty();
                button(label,x+13,y+73,314,34,[this,t]{queue({OrderKind::Research,0,0,t});},canAct()&&valid,player.research==t);
            }
        }
        text("Each branch unlocks from top to bottom. One active project per empire.",179,746,13,muted);
    }
    void help()
    {
        m_buttons.clear();rect(0,0,1440,900,sf::Color(3,8,18,230));rect(240,120,960,660,panel,border);
        text("FIELD MANUAL",279,151,31,cyan);
        const std::array<std::string,13> lines{
            "01   Capture the enemy capital (*) to win. Both players plan simultaneously.",
            "02   Click your planet. BUILD adds structures; FLEET produces ships.",
            "03   Costs are reserved immediately. CLEAR removes all pending orders.",
            "04   FLEET > SEND, then click a connected planet. % selects the fleet fraction.",
            "05   Travel takes 1+ turns. Friendly arrivals merge; hostile arrivals fight.",
            "06   Fighters are quick, frigates durable. Bombers destroy planetary defenses.",
            "07   Mines produce metal; power plants energy; laboratories science.",
            "08   Ships and buildings consume energy. A shortage reduces fleet attack by 35%.",
            "09   RESEARCH opens three technology branches. Heavy ships unlock two hulls.",
            "10   Radar and Deep sensors reveal enemy forces two hyperlanes away.",
            "11   ENTER commits your orders; R opens research; ESC closes panels or exits.",
            "12   Solo: F5 saves, F9 loads, U undoes one turn. Completed turns autosave.",
            "13   LAN: one player hosts; the other enters the host's IPv4. TCP port 4242."
        };
        for(int i=0;i<int(lines.size());++i)text(lines[i],280,215+i*35,17,i%2?muted:white);
        button("BACK TO THE STARS",280,704,840,42,[this]{m_help=false;},true,true);
    }
    sf::RenderWindow m_window;
    sf::Font m_font;
    sf::Texture m_background;
    std::array<sf::Texture,5> m_planets;
    std::vector<Star> m_stars;
    std::vector<Button> m_buttons;
    std::unique_ptr<GameFacade> m_game;
    std::unique_ptr<NetworkSession> m_network;
    std::shared_ptr<VisualEvents> m_effects;
    std::filesystem::path m_save;
    bool m_menu=true,m_research=false,m_help=false,m_ipFocus=false,m_moving=false;
    int m_player=0,m_selected=0,m_aiStyle=0,m_tab=0,m_fraction=100,m_seenTurn=1;
    std::uint32_t m_seed=42;
    float m_time=0;
    std::string m_ip="127.0.0.1",m_status;
    bool m_smoke;
    std::string m_screenshot;
};
}
int main(int argc,char ** argv)
{
    try
    {
        std::filesystem::path assets=GD_ASSET_DIR;bool smoke=false;std::string screenshot="/tmp/galactic-dominion.png";
        auto executableAssets=std::filesystem::absolute(argv[0]).parent_path()/"assets";
        if(std::filesystem::exists(executableAssets))assets=executableAssets;
        for(int i=1;i<argc;++i)
        {
            std::string arg=argv[i];
            if(arg=="--assets"&&i+1<argc)assets=argv[++i];
            else if(arg=="--smoke-test"){smoke=true;if(i+1<argc)screenshot=argv[++i];}
            else if(arg=="--help"){std::cout<<"GalacticDominion [--assets directory] [--smoke-test screenshot.png]\n";return 0;}
            else throw std::runtime_error("Unknown option: "+arg);
        }
        App app(assets,smoke,screenshot);app.run();
    }
    catch(const std::exception & e){std::cerr<<"Galactic Dominion: "<<e.what()<<'\n';return 1;}
}
