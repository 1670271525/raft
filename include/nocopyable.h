#pragma once

namespace flz
{

class Nocopyable{

	public:

		Nocopyable() = default;
		~Nocopyable() = default;
		Nocopyable(const Nocopyable&&) = delete;
		Nocopyable(const Nocopyable&) = delete;
		Nocopyable& operator=(const Nocopyable&)=delete;

};


}
