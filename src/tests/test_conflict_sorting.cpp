#include <QApplication>
#include <QPersistentModelIndex>
#include <QTreeView>
#include "modinfodialogconflictsmodels.h"
#include <gtest/gtest.h>

TEST(ConflictSorting, PopulatedConflictCellsStayFirstInBothDirections)
{
  QTreeView tree;
  AdvancedConflictListModel model(&tree);
  model.add(ConflictItem("", "none", "", 1, "none", false, "", false));
  model.add(ConflictItem("Alpha", "alpha", "Beta", 2, "alpha", true, "", false));
  model.add(ConflictItem("Beta", "beta", "Alpha", 3, "beta", true, "", false));
  model.finished();
  QPersistentModelIndex selected(model.index(1, 1));
  for (const int column : {0, 2}) {
    for (const auto order : {Qt::AscendingOrder, Qt::DescendingOrder}) {
      model.sort(column, order);
      EXPECT_FALSE(model.data(model.index(0, column), Qt::DisplayRole).toString().isEmpty());
      EXPECT_TRUE(model.data(model.index(2, column), Qt::DisplayRole).toString().isEmpty());
      EXPECT_EQ(order == Qt::AscendingOrder ? "Alpha" : "Beta",
                model.data(model.index(0, column), Qt::DisplayRole).toString());
      EXPECT_EQ("alpha", selected.data().toString());
    }
  }
}

TEST(ConflictSorting, FileColumnStillReversesNormally)
{
  QTreeView tree;
  AdvancedConflictListModel model(&tree);
  model.add(ConflictItem("", "a", "", 1, "a", false, "", false));
  model.add(ConflictItem("Beta", "z", "Alpha", 2, "z", true, "", false));
  model.finished();
  model.sort(1, Qt::AscendingOrder);
  EXPECT_EQ("a", model.data(model.index(0, 1), Qt::DisplayRole).toString());
  model.sort(1, Qt::DescendingOrder);
  EXPECT_EQ("z", model.data(model.index(0, 1), Qt::DisplayRole).toString());
}

int main(int argc, char** argv)
{
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
