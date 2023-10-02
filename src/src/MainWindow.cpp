#include "MainWindow.h"
#include "ui_MainWindow.h"

#include "NewTextEdit.h"
#include "MessageWidget.h"
#include "ThemeIcon.h"
#include "ChatItem.h"
#include "Settings.h"
#include "NewListWidgetItem.h"
#include "APIKey.h"

MainWindow::MainWindow(QWidget *parent)
: QMainWindow(parent), m_ui(new Ui::MainWindow)
{
  m_ui->setupUi(this);

  answerState(false);
  errorState(false);
  setChatSettings(0);
  fillChatList();

  ThemeIcon::setIcon(*m_ui->newChatButton, ":/icons/add.svg");
  ThemeIcon::setIcon(*m_ui->settingsButton, ":/icons/settings.svg");
  ThemeIcon::setIcon(*m_ui->retryButton, ":/icons/refresh.svg");
  ThemeIcon::setIcon(*m_ui->stopButton, ":/icons/stop.svg");
  ThemeIcon::setIcon(*m_ui->chatSettingsButton, ":/icons/configuration.svg");
  ThemeIcon::setIcon(*m_ui->sendButton, ":/icons/send.svg");

  connect(m_ui->textInput, &NewTextEdit::sendText,
          this, &MainWindow::receivedText);
  connect(m_ui->sendButton, &QToolButton::clicked,
          this, &MainWindow::sendClicked);
  connect(m_ui->newChatButton, &QToolButton::clicked,
          this, &MainWindow::newChatClicked);
  connect(m_ui->chatList, &QListWidget::currentItemChanged,
          this, &MainWindow::chatItemChanged);
  connect(m_ui->settingsButton, &QToolButton::clicked,
          this, &MainWindow::settingsClicked);
  connect(m_ui->retryButton, &QToolButton::clicked,
          this, &MainWindow::retryClicked);
  connect(m_ui->stopButton, &QToolButton::clicked,
          this, &MainWindow::stopClicked);
  connect(m_ui->chatSettingsButton, &QToolButton::clicked,
          this, &MainWindow::chatSettingsClicked);


  m_movie.setFileName(":/icons/eclipse.gif");
  m_movie.setScaledSize(QSize(32, 32));
  m_ui->loading->setMovie(&m_movie);
  m_ui->historyList->verticalScrollBar()->setSingleStep(20);
  m_answerEffect.setSource(QUrl::fromLocalFile(":/sounds/message.wav"));
  m_answerEffect.setVolume(0.20f);
  m_errorEffect.setSource(QUrl::fromLocalFile(":/sounds/error.wav"));
  m_errorEffect.setVolume(0.15f);

  APIKey key;

  if (key.isNeedKey())
  {
    m_setupDialog = m_setupDialog.create(this);

    connect(m_setupDialog.get(), &SetupDialog::finished, this, [=]()
    {
      m_setupDialog.clear();
    });

    m_setupDialog->show();
  }
}

MainWindow::~MainWindow()
{
  delete m_ui;

  for (ChatGPT *chatGPT : m_chatGPTList)
  {
    chatGPT->deleteLater();
  }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
  QMainWindow::resizeEvent(event);
  emit resized();
}

void MainWindow::setChatSettings(const quint8 &index)
{
  m_chatSettings = m_chatSettings.getSettings(index);
}

void MainWindow::setAutoNameChat()
{
  ChatGPT *chatGPT = qobject_cast<ChatGPT*>(QObject::sender());
  QString name = chatGPT->getAnswerMessage().content[0];
  chatGPT->deleteLater();

  if (name.isEmpty())
  {
    return;
  }

  static QRegularExpression re("\"([\\w\\W]*)\"");
  QRegularExpressionMatchIterator matchIterator = re.globalMatch(name);

  if (matchIterator.hasNext())
  {
    name = matchIterator.next().captured(1);
  }

  while (!name.isEmpty())
  {
    if (name.last(1) == ' ' || name.last(1) == '.'
        || name.last(1) == '\n')
    {
      name.remove(name.length() - 1, 1);
    }
    else
    {
      break;
    }
  }

  if (name.length() < 30 && !name.isEmpty())
  {
    QListWidgetItem *item = m_ui->chatList->currentItem();
    QWidget *itemWidget = m_ui->chatList->itemWidget(item);
    ChatItem *chatItem = dynamic_cast<ChatItem *>(itemWidget);
    chatItem->setName(name);
  }

  sendMessage(m_allMesages);
}

void MainWindow::receivedText(QString text)
{
  if (text.isEmpty() || m_isWaitAnswer ||
     (m_isErrorAnswer && m_ui->roleBox->currentIndex() == 0))
  {
    return;
  }

  HistoryParser history(m_chatSettings.fileName);
  HistoryParser::Message message;
  message.content = text;
  message.role = static_cast<HistoryParser::Role>(
                 m_ui->roleBox->currentIndex());
  addMessages(message, m_ui->chatList->currentRow());
  history.addMessage(message);
  m_allMesages.append(message);
  m_ui->textInput->clear();

  if (message.role != HistoryParser::User)
  {
    errorState(false);
    answerState(false);
    return;
  }

  Settings settings;

  if (m_ui->historyList->count() != 1 || !settings.autoNaming)
  {
    sendMessage(m_allMesages);
    return;
  }

  ChatGPT *chatGPT = new ChatGPT(settings.openAIKey);
  ChatSettings chatSettings;
  chatSettings.stop = {"\n"};

  connect(chatGPT, &ChatGPT::responseReceived,
          this, &MainWindow::setAutoNameChat);
  connect(chatGPT, &ChatGPT::replyError, this, [=]()
  {
    chatGPT->deleteLater();
    sendMessage(m_allMesages);
  });

  chatGPT->send("\"" + message.content + "\"" +
  tr(" - this is the first sentence in the chat, you should understand "
     "what it's about and name the chat according to its topic. "
     "Name it as briefly as possible, but keep the meaning, and try to "
     "use signs only where it is really necessary. Also, you should "
     "not name the chat like \"chat name:\", you should just write the "
     "name without unnecessary words."), chatSettings);
  errorState(false);
  answerState(true);
}

void MainWindow::fillChatList()
{
  if (!checkExistChats())
  {
    return;
  }

  QFile chatJson(QDir::currentPath() + "/chats.json");

  if (chatJson.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    m_ui->chatList->clear();
    QByteArray jsonData = chatJson.readAll();
    chatJson.close();
    QJsonDocument document = QJsonDocument::fromJson(jsonData);
    QJsonArray chats = document.array();

    for (quint8 i = 0; i < chats.count(); ++i)
    {
      QJsonObject object = chats[i].toObject();
      QString name = object["name"].toString();
      addChatItem(name);
    }

    m_ui->chatList->setCurrentRow(0);
    chatItemChanged(m_ui->chatList->item(0));
  }
}

bool MainWindow::checkExistChats()
{
  QFile chatJson(QDir::currentPath() + "/chats.json");

  if (chatJson.exists())
  {
    if (chatJson.open(QIODevice::ReadOnly | QIODevice::Text))
    {
      QByteArray jsonData = chatJson.readAll();
      chatJson.close();
      QJsonDocument document = QJsonDocument::fromJson(jsonData);
      QJsonArray chats = document.array();

      if (!chats.empty())
      {
        return true;
      }
    }
  }

  return createChat(tr("New chat"));
}

void MainWindow::addChatItem(QString name)
{
  NewListWidgetItem *item = new NewListWidgetItem(m_ui->chatList);
  ChatItem *chatItem = new ChatItem(m_ui->chatList, name, item);
  m_ui->chatList->setItemWidget(item, chatItem);
  item->setSizeHint(QSize(0, 50));

  connect(chatItem, &ChatItem::removed,
          this, &MainWindow::chatItemDeleteClicked);
}

bool MainWindow::createChat(QString name)
{
  Settings settingsWidget;
  ChatSettings chatSettings = settingsWidget.chatSettings;
  QFile chatJson(QDir::currentPath() + "/chats.json");
  QJsonObject object;
  object.insert("name", name);
  QString uniqueFileName = QUuid::createUuid().
                           toString(QUuid::WithoutBraces).left(4);
  object.insert("file_name", uniqueFileName);
  object.insert("model", chatSettings.model);
  object.insert("used_tokens", 0);
  object.insert("max_tokens", 4096);
  object.insert("temperature", chatSettings.temperature);
  object.insert("n", chatSettings.n);
  QJsonArray stop = QJsonArray::fromStringList(chatSettings.stop);
  object.insert("stop", stop);
  object.insert("presence_penalty", chatSettings.presencePenalty);
  object.insert("frequency_penalty", chatSettings.frequencyPenalty);

  if (chatJson.open(QIODevice::ReadOnly | QIODevice::Text)
      || !chatJson.exists())
  {
    QByteArray jsonData = chatJson.readAll();
    chatJson.close();

    if (chatJson.open(QIODevice::WriteOnly | QIODevice::Text))
    {
      QJsonDocument document = QJsonDocument::fromJson(jsonData);
      QJsonArray chats = document.array();
      chats.append(object);
      document.setArray(chats);
      chatJson.write(document.toJson());
      chatJson.close();
      addChatItem(name);
      return true;
    }
  }

  return false;
}

void MainWindow::showChat()
{
  m_ui->historyList->clear();
  m_allMesages.clear();
  setChatSettings(m_ui->chatList->currentRow());

  if (m_chatSettings.fileName.isEmpty())
  {
    return;
  }

  HistoryParser history(m_chatSettings.fileName);
  quint16 count = history.getCountMessage();
  tokensLeft();
  m_allMesages.reserve(count);

  for (quint16 i = 0; i < count; ++i)
  {
    m_allMesages.append(history.getMessages(i));
    addMessages(m_allMesages[i], m_ui->chatList->currentRow());
  }

  if (count > 0)
  {
    QListWidgetItem *item =  m_ui->historyList->item(count - 1);
    MessageWidget *message = dynamic_cast<MessageWidget*>(
                            m_ui->historyList->itemWidget(item));

    if (message != nullptr)
    {
      connect(message, &MessageWidget::resizeFinished,
              this, &MainWindow::scrollToBottom);
    }
  }

  for (ChatGPT *chatGPT : m_chatGPTList)
  {
    if (chatGPT->getIndex() == m_ui->chatList->currentRow())
    {
      answerState(!chatGPT->isError());
      errorState(chatGPT->isError());
      return;
    }
  }

  answerState(false);
  errorState(m_allMesages.count() != 0 &&
             m_allMesages.last().role == HistoryParser::Role::User);
}

void MainWindow::answerState(bool waitAnswer)
{
  m_isWaitAnswer = waitAnswer;
  m_movie.setPaused(!waitAnswer);
  m_ui->loading->setVisible(waitAnswer);
  m_ui->stopButton->setVisible(waitAnswer);
}

void MainWindow::errorState(bool error)
{
  m_isErrorAnswer = error;
  m_ui->retryButton->setVisible(error);
}

void MainWindow::tokensLeft()
{
  quint32 maxTokens = m_chatSettings.getMaxTokens();
  quint32 usedTokens = m_chatSettings.usedTokens;
  quint32 tokensLeft = maxTokens - usedTokens;

  if (maxTokens < usedTokens)
  {
    tokensLeft = 0;
  }

  m_ui->countToken->setText(tr("Tokens left") + ": " +
                            QString::number(tokensLeft));
}

void MainWindow::setFileChatSettings(const quint8 &index)
{
  QFile chatJson(QDir::currentPath() + "/chats.json");

  if (chatJson.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    QByteArray jsonData = chatJson.readAll();
    chatJson.close();

    if (chatJson.open(QIODevice::WriteOnly | QIODevice::Text))
    {
      QJsonDocument document = QJsonDocument::fromJson(jsonData);
      QJsonArray chats = document.array();
      QJsonObject object = chats[index].toObject();
      object["name"] = m_chatSettings.name;
      object["file_name"] = m_chatSettings.fileName;
      object["model"] = m_chatSettings.model;
      object["used_tokens"] = (int)m_chatSettings.usedTokens;
      object["max_tokens"] = (int)m_chatSettings.getMaxTokens();
      object["temperature"] = m_chatSettings.temperature;
      object["n"] = m_chatSettings.n;
      object["stop"] = QJsonArray::fromStringList(m_chatSettings.stop);
      object["presence_penalty"] = m_chatSettings.presencePenalty;
      object["frequency_penalty"] = m_chatSettings.frequencyPenalty;
      chats[index] = object;
      document.setArray(chats);
      chatJson.write(document.toJson());
      chatJson.close();
    }
  }
}

void MainWindow::sendMessage(QVector<HistoryParser::Messages> messages,
                             quint16 index)
{
  Settings settings;
  ChatGPT *chatGPT = new ChatGPT(settings.openAIKey);
  m_ui->retryButton->setToolTip("");

  connect(chatGPT, &ChatGPT::responseReceived, this, [=]()
  {
    responseReceived(index);
  });
  connect(chatGPT, &ChatGPT::replyError,
          this, &MainWindow::replyError);

  m_chatGPTList.append(chatGPT);
  answerState(true);
  errorState(false);
  setChatSettings(m_ui->chatList->currentRow());
  ChatSettings chatSettings = m_chatSettings;

  if (index != 0)
  {
    chatSettings.n = 1;
  }

  chatGPT->send(messages, chatSettings, m_ui->chatList->currentRow());
}

void MainWindow::addMessages(HistoryParser::Messages message, quint8 chatIndex)
{
  NewListWidgetItem *item = new NewListWidgetItem(m_ui->historyList);
  MessageWidget *messageWidget = new MessageWidget(item, message, chatIndex);
  m_ui->historyList->setItemWidget(item, messageWidget);
  m_ui->historyList->scrollToBottom();

  connect(this, &MainWindow::resized,
          messageWidget, &MessageWidget::resize);
  connect(messageWidget, &MessageWidget::selfDelete,
          this, &MainWindow::messageDeleteCliked);
  connect(messageWidget, &MessageWidget::selfEdit,
          this, &MainWindow::messageEdit);
  connect(messageWidget, &MessageWidget::generate,
          this, &MainWindow::messageGenerate);
}

void MainWindow::sendClicked()
{
  receivedText(m_ui->textInput->toPlainText());
}

void MainWindow::newChatClicked()
{
  createChat(tr("New chat"));
}

void MainWindow::chatItemChanged(QListWidgetItem *item)
{
  QWidget *itemWidget = m_ui->chatList->itemWidget(item);
  ChatItem *chatItem = dynamic_cast<ChatItem *>(itemWidget);

  if (chatItem != nullptr)
  {
    chatItem->setNewMessage(false);
    showChat();
  }
}

void MainWindow::settingsClicked()
{
  m_settingsWidget = m_settingsWidget.create(this);
  m_settingsWidget->show();
}

void MainWindow::retryClicked()
{
  m_ui->retryButton->setToolTip("");
  answerState(true);
  errorState(false);
  sendMessage(m_allMesages);
}

void MainWindow::stopClicked()
{
  answerState(false);
  errorState(true);

  for ( ChatGPT *chatGPT : m_chatGPTList)
  {
    if (chatGPT->getIndex() == m_ui->chatList->currentRow())
    {
      chatGPT->deleteLater();
      m_chatGPTList.removeOne(chatGPT);
      break;
    }
  }
}

void MainWindow::chatSettingsClicked()
{
  m_chatSettingsDialog = m_chatSettingsDialog.create(
                         this, m_ui->chatList->currentRow());
  m_chatSettingsDialog->show();
}

void MainWindow::responseReceived(quint16 index)
{
  ChatGPT *sender = qobject_cast<ChatGPT*>(QObject::sender());
  m_answerEffect.play();

  disconnect(sender, &ChatGPT::replyError,
             this, &MainWindow::replyError);

  ChatSettings chatSettings = m_chatSettings;
  setChatSettings(sender->getIndex());
  HistoryParser history(m_chatSettings.fileName);

  if (index == 0)
  {
    m_chatSettings.usedTokens = sender->getUsedToken();
    setFileChatSettings(sender->getIndex());
    history.addMessage(sender->getAnswerMessage());
  }
  else
  {
    history.addContent(index, sender->getAnswerMessage().getMessage());
  }

  if (sender->getIndex() == m_ui->chatList->currentRow())
  {
    answerState(false);

    if (index == 0)
    {
      m_allMesages.append(sender->getAnswerMessage());
      addMessages(sender->getAnswerMessage(), m_ui->chatList->currentRow());
      m_chatSettings.usedTokens = sender->getUsedToken();
      tokensLeft();
    }
    else
    {
      m_allMesages[index].addMessage(sender->getAnswerMessage().getMessage());
      QListWidgetItem *item = m_ui->historyList->item(index);
      MessageWidget *nextMessage = dynamic_cast<MessageWidget*>(
                                   m_ui->historyList->itemWidget(item));
      if (nextMessage != nullptr)
      {
        nextMessage->newMessage();
      }
    }
  }
  else
  {
    QListWidgetItem *item = m_ui->chatList->item(sender->getIndex());
    QWidget *itemWidget = m_ui->chatList->itemWidget(item);
    ChatItem *chatItem = dynamic_cast<ChatItem*>(itemWidget);
    chatItem->setNewMessage(true);
    m_chatSettings = chatSettings;
  }

  m_chatGPTList.removeOne(sender);
  sender->deleteLater();
}

void MainWindow::replyError(QString error)
{
  ChatGPT *sender = qobject_cast<ChatGPT*>(QObject::sender());
  disconnect(sender, &ChatGPT::replyError,
          this, &MainWindow::replyError);

  m_errorEffect.play();
  m_ui->retryButton->setToolTip(error);

  if (sender->getIndex() == m_ui->chatList->currentRow())
  {
    answerState(false);
    errorState(true);
  }

  m_chatGPTList.removeOne(sender);
  sender->deleteLater();
}

void MainWindow::chatItemDeleteClicked()
{
  ChatItem *sender = qobject_cast<ChatItem*>(QObject::sender());
  const quint8 &index = sender->getItem()->getIndex();

  disconnect(m_ui->chatList, &QListWidget::currentItemChanged,
             this, &MainWindow::chatItemChanged);

  bool currentItem = m_ui->chatList->currentRow() == index;
  m_ui->chatList->takeItem(index);

  connect(m_ui->chatList, &QListWidget::currentItemChanged,
          this, &MainWindow::chatItemChanged);

  if (checkExistChats())
  {
    if (currentItem)
    {
      m_ui->chatList->setCurrentRow(0);
      chatItemChanged(m_ui->chatList->item(0));
    }
  }

  for (ChatGPT *chatGPT : m_chatGPTList)
  {
    if (chatGPT->getIndex() == index)
    {
      chatGPT->deleteLater();
      m_chatGPTList.removeOne(chatGPT);
      break;
    }
  }
}

void MainWindow::messageDeleteCliked(bool all)
{
  MessageWidget *sender = qobject_cast<MessageWidget*>(QObject::sender());
  const quint8 &index = sender->getItem()->getIndex();

  if (all)
  {
    disconnect(sender, &MessageWidget::selfDelete,
               this, &MainWindow::messageDeleteCliked);

    m_ui->historyList->takeItem(index);
    m_allMesages.removeAt(index);
    bool find = false;

    for (const auto &chatGPT : m_chatGPTList)
    {
      if (chatGPT->getIndex() == m_ui->chatList->currentRow())
      {
        find = true;
        break;
      }
    }

    if (!find)
    {
      errorState(m_allMesages.count() != 0 &&
                 m_allMesages.last().role == HistoryParser::Role::User);
    }

    if (index == 0 && m_ui->historyList->count() > 0)
    {
      QListWidgetItem *item = m_ui->historyList->item(0);
      MessageWidget *nextMessage = dynamic_cast<MessageWidget*>(
                                   m_ui->historyList->itemWidget(item));
      if (nextMessage != nullptr)
      {
        nextMessage->hideGenerate();
      }
    }
  }
  else
  {
    m_allMesages[index] = sender->getMessages();
  }
}

void MainWindow::messageEdit()
{
  MessageWidget *sender = qobject_cast<MessageWidget*>(QObject::sender());
  m_allMesages[sender->getItem()->getIndex()] = sender->getMessages();
}

void MainWindow::messageGenerate(quint16 index)
{
  QVector<HistoryParser::Messages> messages;

  for (quint16 i = 0; i < index; ++i)
  {
    messages.append(m_allMesages[i]);
  }

  sendMessage(messages, index);
}

void MainWindow::scrollToBottom()
{
  MessageWidget *message = qobject_cast<MessageWidget*>(QObject::sender());

  if (message->isTimerResize())
  {
    disconnect(message, &MessageWidget::resizeFinished,
               this, &MainWindow::scrollToBottom);

    m_ui->historyList->scrollToBottom();
  }
}
