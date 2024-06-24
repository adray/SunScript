#include "Sun.h"
#include "SunScript.h"
#include <fstream>
#include <vector>
#include <iostream>
#include <unordered_map>
#include <sstream>
#include <unordered_set>
#include <stack>
#include <filesystem>

using namespace SunScript;

enum class TokenType
{
    OPEN_PARAN,
    CLOSE_PARAN,
    OPEN_BRACE,
    CLOSE_BRACE,
    PLUS,
    MINUS,
    STAR,
    SLASH,
    SEMICOLON,
    COMMA,
    DOT,
    COLON,
    EQUALS,
    EQUALS_EQUALS,
    NOT_EQUALS,
    LESS_EQUALS,
    GREATER_EQUALS,
    AND,
    OR,
    NOT,
    LESS,
    GREATER,
    IDENTIFIER,
    INCREMENT,
    DECREMENT,
    PLUS_EQUALS,
    MINUS_EQUALS,
    STAR_EQUALS,
    SLASH_EQUALS,

    // Keywords

    IF,
    ELSE,
    FUNCTION,
    VAR,
    YIELD,
    RETURN,
    WHILE,

    // Literals

    STRING,
    NUMBER
};

class Token
{
public:
    Token(TokenType type, const std::string& value, int line)
        :
        _type(type),
        _value(value),
        _line(line)
    { }

    inline TokenType Type() const { return _type; }
    inline int Line() const { return _line; }
    inline std::string String() const { return _value; }
    inline double Number() const { return std::strtod(_value.c_str(), 0); }

private:
    TokenType _type;
    std::string _value;
    int _line;
};

//====================
// Scanner
//====================

class Scanner
{
public:
    Scanner();
    void ScanLine(const std::string& line);
    void AddToken(TokenType type, const std::string& value);
    void AddToken(TokenType type);
    inline bool IsError() const { return _isError; }
    inline const std::string& Error() const { return _error; }
    inline int ErrorLine() const { return _lineNum; }
    inline const std::vector<Token>& Tokens() const { return _tokens; }
private:
    char Peek();
    char PeekAhead();
    void Advance();
    void ScanWhitespace();
    void ScanStringLiteral();
    void ScanNumberLiteral();
    void ScanIdentifier();
    void SetError(const std::string& error);
    bool IsDigit(char ch);
    void AddKeyword(const std::string& keyword, TokenType token);
    bool IsLetter(char ch);

    std::string _line;
    int _pos;
    int _lineNum;
    bool _scanning;
    std::vector<Token> _tokens;
    std::string _error;
    bool _isError;
    std::unordered_map<std::string, TokenType> _keywords;
};

Scanner::Scanner()
    : _lineNum(1), _pos(0), _scanning(false), _isError(false)
{
    AddKeyword("if", TokenType::IF);
    AddKeyword("else", TokenType::ELSE);
    AddKeyword("function", TokenType::FUNCTION);
    AddKeyword("var", TokenType::VAR);
    AddKeyword("yield", TokenType::YIELD);
    AddKeyword("return", TokenType::RETURN);
    AddKeyword("while", TokenType::WHILE);
}

void Scanner::AddKeyword(const std::string& keyword, TokenType token)
{
    _keywords.insert(std::pair<std::string, TokenType>(keyword, token));
}

bool Scanner::IsLetter(char ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

void Scanner::ScanIdentifier()
{
    std::string identifier;
    bool scanning = true;
    while (scanning)
    {
        char ch = Peek();
        if (IsLetter(ch) || IsDigit(ch))
        {
            identifier += ch;
            Advance();
        }
        else
        {
            scanning = false;
        }
    }

    const auto& it = _keywords.find(identifier);
    if (it != _keywords.end())
    {
        AddToken(it->second);
    }
    else
    {
        AddToken(TokenType::IDENTIFIER, identifier);
    }
}

bool Scanner::IsDigit(char ch)
{
    bool isDigit = false;
    switch (ch)
    {
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        isDigit = true;
        break;
    }
    return isDigit;
}

void Scanner::SetError(const std::string& error)
{
    if (!_isError)
    {
        _isError = true;
        _error = error;
        _scanning = false;
    }
}

char Scanner::Peek()
{
    return _line[_pos];
}

char Scanner::PeekAhead()
{
    if (_pos + 1 < _line.size()) return _line[_pos + 1];
    else return '\0';
}

void Scanner::Advance()
{
    if (_scanning)
    {
        _pos++;
        _scanning = _pos < _line.size();
    }
}

void Scanner::ScanWhitespace()
{
    bool scanning = true;
    while (scanning && _scanning)
    {
        char ch = Peek();
        switch (ch)
        {
        case ' ':
        case '\t':
            Advance();
            break;
        default:
            scanning = false;
            break;
        }
    }
}

void Scanner::ScanStringLiteral()
{
    std::string str;
    bool scanning = true;
    while (scanning)
    {
        char ch = Peek();
        if (ch == '\"')
        {
            AddToken(TokenType::STRING, str);
            Advance();
            scanning = false;
        }
        else
        {
            str += ch;
            Advance();
            if (!_scanning)
            {
                SetError("Ill formed string literal.");
                scanning = false;
            }
        }
    }
}

void Scanner::ScanNumberLiteral()
{
    std::string str;
    bool scanning = true;
    bool hasDot = false;
    while (scanning)
    {
        char ch = Peek();
        if (IsDigit(ch))
        {
            str += ch;
            Advance();
        }
        else if (ch == '.')
        {
            if (!hasDot) {
                hasDot = true;
                str += ch;
                Advance();
            }
            else
            {
                SetError("Invalid numeric literal.");
                scanning = false;
            }
        }
        else
        {
            AddToken(TokenType::NUMBER, str);
            scanning = false;
        }
    }
}

void Scanner::ScanLine(const std::string& line)
{
    if (_isError) { return; }

    _pos = 0;
    _line = line;
    _scanning = true;

    while (_scanning)
    {
        ScanWhitespace();

        char ch = Peek();

        switch (ch)
        {
            case '=':
                Advance();
                if (Peek() == '=') { Advance(); AddToken(TokenType::EQUALS_EQUALS); }
                else { AddToken(TokenType::EQUALS); }
                break;
            case '!':
                Advance();
                if (Peek() == '=') { Advance(); AddToken(TokenType::NOT_EQUALS); }
                else { AddToken(TokenType::NOT); }
                break;
            case '<':
                Advance();
                if (Peek() == '=') { Advance(); AddToken(TokenType::LESS_EQUALS); }
                else { AddToken(TokenType::LESS); }
                break;
            case '>':
                Advance();
                if (Peek() == '=') { Advance(); AddToken(TokenType::GREATER_EQUALS); }
                else { AddToken(TokenType::GREATER); }
                break;
            case ':':
                AddToken(TokenType::COLON);
                Advance();
                break;
            case ';':
                AddToken(TokenType::SEMICOLON);
                Advance();
                break;
            case ',':
                AddToken(TokenType::COMMA);
                Advance();
                break;
            case '(':
                AddToken(TokenType::OPEN_PARAN);
                Advance();
                break;
            case ')':
                AddToken(TokenType::CLOSE_PARAN);
                Advance();
                break;
            case '{':
                AddToken(TokenType::OPEN_BRACE);
                Advance();
                break;
            case '}':
                AddToken(TokenType::CLOSE_BRACE);
                Advance();
                break;
            case '+':
                Advance();
                if (Peek() == '+') { Advance(); AddToken(TokenType::INCREMENT); }
                else if (Peek() == '=') { Advance(); AddToken(TokenType::PLUS_EQUALS); }
                else { AddToken(TokenType::PLUS); }
                break;
            case '-':
                Advance();
                if (Peek() == '-') { Advance(); AddToken(TokenType::DECREMENT); }
                else if (Peek() == '=') { Advance(); AddToken(TokenType::MINUS_EQUALS); }
                else { AddToken(TokenType::MINUS); }
                break;
            case '*':
                Advance();
                if (Peek() == '=') { Advance(); AddToken(TokenType::STAR_EQUALS); }
                else { AddToken(TokenType::STAR); }
                break;
            case '/':
                Advance();
                if (Peek() == '/') { _scanning = false; } // comment
                else if (Peek() == '=') { Advance(); AddToken(TokenType::SLASH_EQUALS); }
                else { AddToken(TokenType::SLASH); }
                break;
            case '\"':
                Advance();
                ScanStringLiteral();
                break;
            case '&':
                Advance();
                if (Peek() == '&')
                {
                    AddToken(TokenType::AND);
                    Advance();
                }
                else
                {
                    SetError("Bitwise AND not supported.");
                }
                break;
            case '|':
                Advance();
                if (Peek() == '|')
                {
                    AddToken(TokenType::OR);
                    Advance();
                }
                else
                {
                    SetError("Bitwise OR not supported.");
                }
                break;
            default:
                if (IsDigit(ch))
                {
                    ScanNumberLiteral();
                }
                else if (IsLetter(ch))
                {
                    ScanIdentifier();
                }
                else if (ch == '.')
                {
                    if (IsDigit(PeekAhead()))
                    {
                        ScanNumberLiteral();
                    }
                    else
                    {
                        AddToken(TokenType::DOT);
                        Advance();
                    }
                }
                else if (ch == '\0')
                {
                    Advance();
                }
                else
                {
                    SetError("Unexcepted character.");
                }
                break;
        }
    }

    if (!IsError())
    {
        _lineNum++;
    }
}

void Scanner::AddToken(TokenType type, const std::string& value)
{
    Token token(type, value, _lineNum);
    _tokens.push_back(token);
}

void Scanner::AddToken(TokenType type)
{
    Token token(type, "", _lineNum);
    _tokens.push_back(token);
}

//===================
// Call
//===================

class Expr;

class Call
{
public:
    Call() : _yield(false) {};
    void PushArg(Expr* expr);
    inline std::vector<Expr*>& Args() { return _args; }
    inline void SetYield() { _yield = true; }
    inline bool Yield() const { return _yield; }

private:
    std::vector<Expr*> _args;
    bool _yield;
};

void Call::PushArg(Expr* expr)
{
    _args.push_back(expr);
}

//===================
// Expr
//===================

class Expr
{
public:
    Expr(Expr* left, Expr* right, Token operation)
    :
        _left(left),
        _right(right),
        _operation(operation),
        _call(nullptr)
    {
    }

    inline Expr* Left() { return _left; }
    inline Expr* Right() { return _right; }
    inline Token Op() { return _operation; }
    inline Call* GetCall() { return _call; }
    inline void SetCall(Call* call) { _call = call; }

private:
    Expr* _left;
    Expr* _right;
    Call* _call;
    Token _operation;
};

//====================
// Parser
//====================

class Parser
{
public:
    Parser(const std::vector<Token>& tokens);
    void Parse();
    inline bool IsError() const { return _isError; }
    inline const std::string& Error() const { return _errorText; }
    inline int ErrorLine() const { return _errorLine; }
    inline Program* GetProgram() { return _program; }

private:

    struct StackFrame
    {
        std::unordered_set<std::string> _vars;
        std::stack<std::unordered_set<std::string>> _scope;
    };

    Token Peek();
    void Advance();
    bool Match(TokenType type);
    void SetError(const std::string& text);
    void EmitExpr(Expr* expr);
    void FreeExpr(Expr* expr);
    void ParseVar();
    void ParseWhile();
    void ParseIfStatement();
    void ParseElse();
    void ParseStatement();
    void ParseAssignment();
    void ParseFunction();
    void ParseReturn();
    void ParseYield();
    void ParseParameter(std::vector<std::string>& params);
    Expr* ParseExprStatement();
    Expr* ParseExpression();
    Expr* ParseEquality();
    Expr* ParseLogicalAnd();
    Expr* ParseLogicalOr();
    Expr* ParseComparision();
    Expr* ParseTerm();
    Expr* ParseFactor();
    Expr* ParseUnary();
    Expr* ParsePrimary();
    Expr* ParseCall();
    Call* ParseArgument();

    bool _scanning;
    int _pos;
    const std::vector<Token>& _tokens;
    bool _isError;
    std::string _errorText;
    int _errorLine;
    Program* _program;
    bool _emitCall;

    std::stack<StackFrame> _frames;
    std::stack<int> _stack;  
};

constexpr int ST_IF = 0x0;
constexpr int ST_ELSE = 0x1; // stack indicating that 'else' clause is added
constexpr int ST_WHILE = 0x2;

Parser::Parser(const std::vector<Token>& tokens)
    : _tokens(tokens),
    _scanning(false),
    _pos(0),
    _isError(false),
    _emitCall(false),
    _errorLine(0)
{
    _program = CreateProgram();
    _frames.push(StackFrame());
    _frames.top()._scope.push(std::unordered_set<std::string>());
}

void Parser::EmitExpr(Expr* expr)
{
    bool binary = expr->Left() && expr->Right();
    
    if (expr->Right())
    {
        EmitExpr(expr->Right());
    }

    if (expr->Left())
    {
        EmitExpr(expr->Left());
    }

    Token tok = expr->Op();
    switch (tok.Type())
    {
    case TokenType::EQUALS_EQUALS:
        EmitEquals(_program);
        break;
    case TokenType::NOT_EQUALS:
        EmitNotEquals(_program);
        break;
    case TokenType::INCREMENT:
        EmitIncrement(_program);
        break;
    case TokenType::DECREMENT:
        EmitDecrement(_program);
        break;
    case TokenType::PLUS:
        EmitAdd(_program);
        break;
    case TokenType::STAR:
        EmitMul(_program);
        break;
    case TokenType::MINUS:
        if (binary)
        {
            EmitSub(_program);
        }
        else
        {
            EmitUnaryMinus(_program);
        }
        break;
    case TokenType::SLASH:
        EmitDiv(_program);
        break;
    case TokenType::GREATER:
        EmitGreaterThan(_program);
        break;
    case TokenType::LESS:
        EmitLessThan(_program);
        break;
    case TokenType::GREATER_EQUALS:
        EmitGreaterThanOrEqual(_program);
        break;
    case TokenType::LESS_EQUALS:
        EmitLessThanOrEqual(_program);
        break;
    case TokenType::OR:
        EmitOr(_program);
        break;
    case TokenType::AND:
        EmitAnd(_program);
        break;
    case TokenType::STRING:
        EmitPush(_program, tok.String());
        break;
    case TokenType::NUMBER:
        EmitPush(_program, (int)tok.Number());
        break;
    case TokenType::IDENTIFIER:
        if (expr->GetCall())
        {
            Call* call = expr->GetCall();
            auto& args = call->Args();
            for (int i = int(args.size()) - 1; i >= 0; i--)
            {
                EmitExpr(args[i]);
            }

            if (call->Yield())
            {
                EmitDebug(_program, tok.Line());
                EmitYield(_program, tok.String(), unsigned char(args.size()));
            }
            else
            {
                EmitDebug(_program, tok.Line());
                EmitCall(_program, tok.String(), unsigned char(args.size()));
            }
            _emitCall = true;
        }
        else
        {
            EmitDebug(_program, tok.Line());
            EmitPushLocal(_program, tok.String());

            // If we happened to just make a function call, clear the 
            // emit flag to indicate we have consumed the return value.
            _emitCall = false;
        }
        break;
    default:
        SetError("Unexpected token '" + tok.String() + "' emitting expression.");
        break;
    }
}

void Parser::FreeExpr(Expr* expr)
{
    if (expr->Right())
    {
        FreeExpr(expr->Right());
    }

    if (expr->Left())
    {
        FreeExpr(expr->Left());
    }

    if (expr->GetCall())
    {
        delete expr->GetCall();
        expr->SetCall(nullptr);
    }

    delete expr;
}

void Parser::SetError(const std::string& text)
{
    if (!_isError)
    {
        _isError = true;
        _errorText = text;
        _scanning = false;
        _errorLine = _tokens[std::min(int(_tokens.size()) - 1, _pos)].Line();
    }
}

Token Parser::Peek()
{
    return _tokens[_pos];
}

void Parser::Advance()
{
    if (_scanning)
    {
        _pos++;
        _scanning = _pos < _tokens.size();
    }
}

bool Parser::Match(TokenType type)
{
    return _scanning && Peek().Type() == type;
}

void Parser::ParseYield()
{
    Advance();

    Expr* expr = ParseCall();
    if (expr)
    {
        expr->GetCall()->SetYield();

        EmitExpr(expr);
        FreeExpr(expr);
    }
    else
    {
        SetError("Unexcepted token.");
    }
}

void Parser::ParseReturn()
{
    if (_frames.size() > 1)
    {
        if (Match(TokenType::RETURN))
        {
            Advance();
            Expr* expr = ParseExprStatement();
            if (expr)
            {
                EmitExpr(expr);
                FreeExpr(expr);

                EmitReturn(_program);
            }
            else
            {
                EmitReturn(_program);
            }
        }
    }
    else
    {
        SetError("Unexpected return statement.");
    }
}

void Parser::ParseParameter(std::vector<std::string>& params)
{
    if (Match(TokenType::IDENTIFIER))
    {
        Token tok = Peek();
        Advance();

        params.push_back(tok.String());

        while (Match(TokenType::COMMA))
        {
            Advance();
            if (Match(TokenType::IDENTIFIER))
            {
                Token tok = Peek();
                Advance();

                params.push_back(tok.String());
            }
            else
            {
                SetError("Missing parameter identifier.");
                break;
            }
        }
    }
}

void Parser::ParseFunction()
{
    if (Match(TokenType::FUNCTION))
    {
        Advance();

        if (Match(TokenType::IDENTIFIER))
        {
            Token token = Peek();
            Advance();
        
            if (Match(TokenType::OPEN_PARAN))
            {
                Advance();

                std::vector<std::string> params;
                ParseParameter(params);
                EmitBeginFunction(_program, token.String(), int(params.size()));

                for (int i = 0; i < int(params.size()); i++)
                {
                    auto& param = params[i];
                    EmitLocal(_program, param);
                    EmitPop(_program, param);
                }
            }
            else
            {
                SetError("Unexpected token, expected '('");
            }
        }
        else
        {
            SetError("Unexpected token, expected the name of the function.");
        }
    
        if (!IsError())
        {
            if (Match(TokenType::CLOSE_PARAN))
            {
                Advance();

                if (Match(TokenType::OPEN_BRACE))
                {
                    Advance();
                    _frames.push(StackFrame());
                    _frames.top()._scope.push(std::unordered_set<std::string>());
                }
                else
                {
                    SetError("Unexpected token, expected '{'");
                }
            }
            else
            {
                SetError("Unexpected token, expected ')'");
            }
        }
    }
}

Expr* Parser::ParseCall()
{
    Expr* expr = ParsePrimary();
    if (Match(TokenType::OPEN_PARAN))
    {
        Advance();

        Call* call = ParseArgument();
        expr->SetCall(call);

        if (Match(TokenType::CLOSE_PARAN))
        {
            Advance();
        }
    }
    if (Match(TokenType::INCREMENT) || Match(TokenType::DECREMENT))
    {
        Token token = Peek();
        Advance();
        expr = new Expr(nullptr, expr, token);
    }
    return expr;
}

Call* Parser::ParseArgument()
{
    Call* call = new Call();
    Expr* expr = ParseExpression();
    if (expr)
    {
        call->PushArg(expr);
        while (Match(TokenType::COMMA))
        {
            Advance();
            call->PushArg(ParseExpression());
        }
    }
    return call;
}

Expr* Parser::ParsePrimary()
{
    if (Match(TokenType::STRING))
    {
        Token str = Peek();
        Advance();
        return new Expr(nullptr, nullptr, str);
    }
    else if (Match(TokenType::NUMBER))
    {
        Token number = Peek();
        Advance();
        return new Expr(nullptr, nullptr, number);
    }
    else if (Match(TokenType::IDENTIFIER))
    {
        Token id = Peek();
        Advance();
        return new Expr(nullptr, nullptr, id);
    }
    else if (Match(TokenType::OPEN_PARAN))
    {
        Advance();
        Expr* expr = ParseExpression();

        bool cont = Match(TokenType::CLOSE_PARAN);
        if (cont)
        {
            Advance();
            return expr;
        }
        else
        {
            delete expr;
            SetError("Unexpected token.");
        }
    }

    return nullptr;
}

Expr* Parser::ParseUnary()
{
    if (Match(TokenType::MINUS) || Match(TokenType::NOT))
    {
        Token op = Peek();
        Advance();
        Expr* right = ParseUnary();
        return new Expr(nullptr, right, op);
    }

    return ParseCall();
}

Expr* Parser::ParseFactor()
{
    Expr* expr = ParseUnary();

    while (Match(TokenType::SLASH) || Match(TokenType::STAR))
    {
        Token op = Peek();
        Advance();
        expr = new Expr(expr, ParseUnary(), op);
    }

    return expr;
}

Expr* Parser::ParseTerm()
{
    Expr* expr = ParseFactor();

    while (Match(TokenType::PLUS) || Match(TokenType::MINUS))
    {
        Token op = Peek();
        Advance();
        expr = new Expr(expr, ParseFactor(), op);
    }

    return expr;
}

Expr* Parser::ParseLogicalAnd()
{
    Expr* expr = ParseEquality();

    while (Match(TokenType::AND))
    {
        Token op = Peek();
        Advance();
        expr = new Expr(expr, ParseEquality(), op);
    }

    return expr;
}

Expr* Parser::ParseLogicalOr()
{
    Expr* expr = ParseLogicalAnd();

    while (Match(TokenType::OR))
    {
        Token op = Peek();
        Advance();
        expr = new Expr(expr, ParseLogicalAnd(), op);
    }

    return expr;
}

Expr* Parser::ParseComparision()
{
    Expr* expr = ParseTerm();

    while (Match(TokenType::GREATER) || Match(TokenType::GREATER_EQUALS) || Match(TokenType::LESS) || Match(TokenType::LESS_EQUALS))
    {
        Token op = Peek();
        Advance();
        expr = new Expr(expr, ParseTerm(), op);
    }

    return expr;
}

Expr* Parser::ParseEquality()
{
    Expr* expr = ParseComparision();
    while (Match(TokenType::EQUALS_EQUALS) || Match(TokenType::NOT_EQUALS))
    {
        Token equals = Peek();
        Advance();
        Expr* right = ParseComparision();
        expr = new Expr(expr, right, equals);
    }

    return expr;
}

Expr* Parser::ParseExpression()
{
    return ParseLogicalOr();
}

void Parser::ParseIfStatement()
{
    Expr* expr = nullptr;
    if (Match(TokenType::OPEN_PARAN))
    {
        Token token = Peek();
        Advance();
        expr = ParseExpression();

        if (Match(TokenType::CLOSE_PARAN))
        {
            Advance();
        }
        else
        {
            SetError("Unexcepted token.");
        }

        if (!IsError() && Match(TokenType::OPEN_BRACE))
        {
            Advance();

            EmitExpr(expr);
            EmitIf(_program);
            FreeExpr(expr);

            _stack.push(ST_IF);
            _frames.top()._scope.push(std::unordered_set<std::string>());
        }
    }
    else
    {
        SetError("Unexcepted token.");
    }
}

Expr* Parser::ParseExprStatement()
{
    Expr* expr = ParseExpression();
    if (Match(TokenType::SEMICOLON))
    {
        Advance();
        return expr;
    }
    else
    {
        SetError("Unexpected token occured parsing statement.");
    }
    return expr;
}

void Parser::ParseStatement()
{
    if (Match(TokenType::IDENTIFIER))
    {
        Token identifier = Peek();
        StackFrame& frame = _frames.top();
        if (frame._vars.find(identifier.String()) != frame._vars.end())
        {
            ParseAssignment();
            return;
        }
    }

    Expr* expr = ParseExprStatement();
    if (expr)
    {
        EmitExpr(expr);
        FreeExpr(expr);

        // If we called a function and its return value wasn't used.
        // Lets get rid of it. In the cases it doesn't return something
        // EmitPop just won't do anything so that is ok as well.
        if (_emitCall)
        {
            EmitPop(_program);
            _emitCall = false;
        }
    }
}

void Parser::ParseAssignment()
{
    Token identifier = Peek();
    Advance();

    if (Match(TokenType::EQUALS))
    {
        Advance();
        Expr* expr = ParseExpression();

        if (Match(TokenType::SEMICOLON))
        {
            EmitExpr(expr);
            EmitPop(_program, identifier.String());
            FreeExpr(expr);

            Advance();
        }
        else
        {
            SetError("Unexcepted token.");
        }
    }
    else if (Match(TokenType::INCREMENT))
    {
        Advance();

        if (Match(TokenType::SEMICOLON))
        {
            EmitPushLocal(_program, identifier.String());
            EmitIncrement(_program);
            EmitPop(_program, identifier.String());
        }
        else
        {
            SetError("Unexcepted token.");
        }
    }
    else if (Match(TokenType::DECREMENT))
    {
        Advance();

        if (Match(TokenType::SEMICOLON))
        {
            EmitPushLocal(_program, identifier.String());
            EmitDecrement(_program);
            EmitPop(_program, identifier.String());
        }
        else
        {
            SetError("Unexcepted token.");
        }
    }
    else if (Match(TokenType::PLUS_EQUALS) || Match(TokenType::MINUS_EQUALS) || Match(TokenType::STAR_EQUALS) || Match(TokenType::SLASH_EQUALS))
    {
        Token token = Peek();

        Advance();
        Expr* expr = ParseExpression();

        if (Match(TokenType::SEMICOLON))
        {
            EmitExpr(expr);
            EmitPushLocal(_program, identifier.String());
            if (token.Type() == TokenType::MINUS_EQUALS)
            {
                EmitSub(_program);
            }
            else if (token.Type() == TokenType::PLUS_EQUALS)
            {
                EmitAdd(_program);
            }
            else if (token.Type() == TokenType::STAR_EQUALS)
            {
                EmitMul(_program);
            }
            else if (token.Type() == TokenType::SLASH_EQUALS)
            {
                EmitDiv(_program);
            }
            EmitPop(_program, identifier.String());
            FreeExpr(expr);

            Advance();
        }
        else
        {
            SetError("Unexcepted token.");
        }
    }
    else
    {
        SetError("Unexcepted token.");
    }
}

void Parser::ParseVar()
{
    if (Match(TokenType::IDENTIFIER))
    {
        Token identifier = Peek();
        Advance();

        StackFrame& top = _frames.top();
        if (top._vars.find(identifier.String()) == top._vars.end())
        {
            if (Match(TokenType::EQUALS))
            {
                Advance();

                Expr* expr = ParseExpression();

                if (Match(TokenType::SEMICOLON))
                {
                    EmitExpr(expr);
                    EmitLocal(_program, identifier.String());
                    EmitPop(_program, identifier.String());
                    FreeExpr(expr);

                    StackFrame& frame = _frames.top();
                    frame._vars.insert(identifier.String());
                    frame._scope.top().insert(identifier.String());

                    Advance();
                }
                else
                {
                    SetError("Unexcepted token.");
                }
            }
            else if (Match(TokenType::SEMICOLON))
            {
                Advance();
                EmitLocal(_program, identifier.String());
            }
            else
            {
                SetError("Unexcepted token.");
            }
        }
        else
        {
            SetError("Redefinition of variable " + identifier.String());
        }
    }
    else
    {
        SetError("Unexcepted token.");
    }
}

void Parser::ParseElse()
{
    Advance();
    if (Match(TokenType::ELSE))
    {
        Advance();
        if (!_stack.top())
        {
            if (Match(TokenType::OPEN_BRACE))
            {
                Advance();

                // Replace the current value now we are in an else
                _stack.pop();
                _stack.push(ST_ELSE);
                _frames.top()._scope.push(std::unordered_set<std::string>());

                EmitElse(_program);
            }
            else if (Match(TokenType::IF))
            {
                Advance();

                _stack.pop();
                EmitElseIf(_program);

                ParseIfStatement();
            }
            else
            {
                SetError("Unexpected token after ELSE clause.");
            }
        }
        else
        {
            SetError("Unexpected else clause");
        }
    }
    else
    {
        _stack.pop();
        EmitEndIf(_program);
    }
}

void Parser::ParseWhile()
{
    EmitLoop(_program);

    Expr* expr = nullptr;
    if (Match(TokenType::OPEN_PARAN))
    {
        Token token = Peek();
        Advance();
        expr = ParseExpression();

        if (Match(TokenType::CLOSE_PARAN))
        {
            Advance();
        }
        else
        {
            SetError("Unexcepted token.");
        }

        if (!IsError() && Match(TokenType::OPEN_BRACE))
        {
            Advance();

            EmitExpr(expr);
            EmitIf(_program);
            FreeExpr(expr);

            _stack.push(ST_WHILE);
            _frames.top()._scope.push(std::unordered_set<std::string>());
        }
    }
    else
    {
        SetError("Unexcepted token.");
    }
}

void Parser::Parse()
{
    _scanning = true;

    while (_scanning)
    {
        Token token = Peek();
        switch (token.Type())
        {
        case TokenType::WHILE:
            Advance();
            ParseWhile();
            break;
        case TokenType::IF:
            Advance();
            ParseIfStatement();
            break;
        case TokenType::VAR:
            Advance();
            ParseVar();
            break;
        case TokenType::CLOSE_BRACE:
            if (_stack.size() > 0)
            {
                const int val = _stack.top();

                if (val == ST_IF || val == ST_ELSE)
                {
                    auto& top = _frames.top();
                    for (auto& item : top._scope.top())
                    {
                        top._vars.erase(item);
                    }

                    _frames.top()._scope.pop();
                    ParseElse();
                }
                else if (val == ST_WHILE)
                {
                    // while loop?
                    auto& top = _frames.top();
                    for (auto& item : top._scope.top())
                    {
                        top._vars.erase(item);
                    }

                    _frames.top()._scope.pop();
                    _stack.pop();

                    Advance();
                    EmitEndLoop(_program);
                }
            }
            else if (_frames.size() > 1)
            {
                Advance();
                EmitEndFunction(_program);

                _frames.pop();
            }
            else
            {
                SetError("Unexpected close brace.");
            }
            break;
        case TokenType::FUNCTION:
            ParseFunction();
            break;
        case TokenType::RETURN:
            ParseReturn();
            break;
        case TokenType::YIELD:
            ParseYield();
            break;
        default:
            ParseStatement();
            break;
        }
    }

    if (_stack.size() != 0)
    {
        SetError("Expected close brace.");
    }

    if (!_isError)
    {
        EmitDone(_program);
    }
}

//====================

static SunScript::Program* CompileFile2(const std::string& filepath, std::string* error)
{
    SunScript::Program* program = nullptr;

    std::ifstream stream;
    stream.open(filepath);
    if (stream.good())
    {
        const int size = 512;
        char line[size];

        Scanner scanner;

        while (!stream.eof())
        {
            stream.getline(line, size);
            scanner.ScanLine(line);
        }

        if (scanner.IsError())
        {
            if (error)
            {
                std::stringstream ss;
                ss << "Error Line: " << scanner.ErrorLine() << " " << scanner.Error();

                *error = ss.str();
            }
        }
        else
        {
            Parser parser(scanner.Tokens());
            parser.Parse();

            if (parser.IsError() && error)
            {
                std::stringstream ss;
                ss << "Error Line: " << parser.ErrorLine() << " " << parser.Error();

                *error = ss.str();
            }
            else
            {
                program = parser.GetProgram();
            }
        }
    }
    else
    {
        std::stringstream ss;
        ss << "File not found.";

        *error = ss.str();
    }

    return program;
}

void SunScript::CompileFile(const std::string& filepath, unsigned char** programData)
{
    CompileFile(filepath, programData, nullptr, nullptr);
}

void SunScript::CompileFile(const std::string& filepath, unsigned char** programData, unsigned char** debugData, std::string* error)
{
    Program* program = CompileFile2(filepath, error);

    *programData = nullptr;

    if (program)
    {
        GetProgram(program, programData);

        if (debugData)
        {
            GetDebugData(program, debugData);
        }
    }
}

//==========================
// Sun compilier
//==========================

#ifdef _SUN_EXECUTABLE_
#include <iostream>
#include "SunScriptDemo.h"
    static void PrintHelp()
    {
        std::cout << "Usage:" << std::endl;
        std::cout << "Sun build <file1> <file2>..." << std::endl;
        std::cout << "Sun disassemble <file1>" << std::endl;
        std::cout << "Sun demo" << std::endl;
    }

    static void Build(int numFiles, char** files)
    {
        for (int i = 0; i < numFiles; i++)
        {
            std::string filename = files[i];
            std::cout << "[" << filename << "] ";

            std::string error;
            
            Program* program = CompileFile2(filename, &error);
            if (program)
            {
                unsigned char* programData;
                unsigned char* debugData;
                const int versionNumber = 0;
                const int programDataSize = GetProgram(program, &programData);
                const int debugDataSize   = GetDebugData(program, &debugData);

                std::filesystem::path path = filename;
                path.replace_extension("obj");

                std::ofstream ss;
                ss.open(path, std::ios::binary | std::ios::trunc);
                ss.write((char*)&versionNumber, sizeof(int));
                ss.write((char*)&programDataSize, sizeof(int));
                ss.write((char*)programData, programDataSize);
                ss.write((char*)&debugDataSize, sizeof(int));
                ss.write((char*)debugData, debugDataSize);
                ss.close();

                std::cout << "Script built successfully" << std::endl;
            }
            else
            {
                std::cout << "Failed to compile script: " << error << std::endl;
            }
        }
    }

    static void DisassembleProgram(const std::string& file)
    {
        unsigned char* program;
        LoadScript(file, &program);
        std::cout << "Script loaded" << std::endl;
        if (program)
        {
            std::stringstream ss;
            Disassemble(ss, program, nullptr);
            std::cout << ss.str();

            delete[] program;
        }
        else
        {
            std::cout << "Failed to load program." << std::endl;
        }
    }

    int main(int numArgs, char** args)
    {
        if (numArgs <= 1)
        {
            PrintHelp();
        }
        else
        {
            std::cout << "SunScript Compiler" << std::endl;

            std::string cmd = args[1];
            if (cmd == "build")
            {
                Build(numArgs - 2, args + 2);
            }
            else if (cmd == "disassemble")
            {
                if (numArgs <= 2)
                {
                    std::cout << "No program data specified." << std::endl;
                }
                else
                {
                    DisassembleProgram(args[2]);
                }
            }
            else if (cmd == "demo")
            {
                std::cout << "Demos:" << std::endl;
                std::cout << "sun demo1" << std::endl;
                std::cout << "sun demo2" << std::endl;
                std::cout << "sun demo3" << std::endl;
                std::cout << "sun demo4" << std::endl;
                std::cout << "sun demo5" << std::endl;
            }
            else if (cmd == "demo1")
            {
                std::cout << "Running Demo1()" << std::endl;
                SunScript::Demo1(42);
            }
            else if (cmd == "demo2")
            {
                std::cout << "Running Demo2()" << std::endl;
                SunScript::Demo2();
            }
            else if (cmd == "demo3")
            {
                std::cout << "Running Demo3()" << std::endl;
                SunScript::Demo3();
            }
            else if (cmd == "demo4")
            {
                std::cout << "Running Demo4()" << std::endl;
                SunScript::Demo4();
            }
            else if (cmd == "demo5")
            {
                std::cout << "Running Demo5()" << std::endl;
                SunScript::Demo5();
            }
            else
            {
                std::cout << "Invalid command." << std::endl;
            }
        }
        
        return 0;
    }
#endif

//=====================
